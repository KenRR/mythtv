/** -*- Mode: c++ -*-
 *  HLSStreamHandler
 *  Copyright (c) 2013 Bubblestuff Pty Ltd
 *  based on IPTVStreamHandler
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#include <chrono> // for milliseconds
#include <thread> // for sleep_for

// MythTV headers
#include "hlsstreamhandler.h"
#include "mythlogging.h"
#include "recorders/HLS/HLSReader.h"

#define LOC QString("HLSSH[%1](%2): ").arg(_inputid).arg(_device)

// BUFFER_SIZE is a multiple of TS_SIZE
#define TS_SIZE     188
#define BUFFER_SIZE (512 * TS_SIZE)

QMap<QString,HLSStreamHandler*>  HLSStreamHandler::s_hlshandlers;
QMap<QString,uint>               HLSStreamHandler::s_hlshandlers_refcnt;
QMutex                           HLSStreamHandler::s_hlshandlers_lock;

HLSStreamHandler* HLSStreamHandler::Get(const IPTVTuningData& tuning, int inputid)
{
    QMutexLocker locker(&s_hlshandlers_lock);

    QString devkey = tuning.GetDeviceKey();

    QMap<QString,HLSStreamHandler*>::iterator it = s_hlshandlers.find(devkey);

    if (it == s_hlshandlers.end())
    {
        HLSStreamHandler* newhandler = new HLSStreamHandler(tuning, inputid);
        newhandler->Start();
        s_hlshandlers[devkey] = newhandler;
        s_hlshandlers_refcnt[devkey] = 1;

        LOG(VB_RECORD, LOG_INFO,
            QString("HLSSH[%1]: Creating new stream handler %2 for %3")
            .arg(inputid).arg(devkey).arg(tuning.GetDeviceName()));
    }
    else
    {
        s_hlshandlers_refcnt[devkey]++;
        uint rcount = s_hlshandlers_refcnt[devkey];
        LOG(VB_RECORD, LOG_INFO,
            QString("HLSSH[%1]: Using existing stream handler %2 for %3")
            .arg(inputid).arg(devkey).arg(tuning.GetDeviceName()) +
            QString(" (%1 in use)").arg(rcount));
    }

    return s_hlshandlers[devkey];
}

void HLSStreamHandler::Return(HLSStreamHandler* & ref, int inputid)
{
    QMutexLocker locker(&s_hlshandlers_lock);

    QString devname = ref->_device;

    QMap<QString,uint>::iterator rit = s_hlshandlers_refcnt.find(devname);
    if (rit == s_hlshandlers_refcnt.end())
        return;

    LOG(VB_RECORD, LOG_INFO, QString("HLSSH[%1]: Return(%2) has %3 handlers")
        .arg(inputid).arg(devname).arg(*rit));

    if (*rit > 1)
    {
        ref = nullptr;
        (*rit)--;
        return;
    }

    QMap<QString,HLSStreamHandler*>::iterator it = s_hlshandlers.find(devname);
    if ((it != s_hlshandlers.end()) && (*it == ref))
    {
        LOG(VB_RECORD, LOG_INFO, QString("HLSSH[%1]: Closing handler for %2")
                           .arg(devname));
        ref->Stop();
        LOG(VB_RECORD, LOG_DEBUG, QString("HLSSH[%1]: handler for %2 stopped")
            .arg(inputid).arg(devname));
        delete *it;
        s_hlshandlers.erase(it);
    }
    else
    {
        LOG(VB_GENERAL, LOG_ERR,
            QString("HLSSH[%1] Error: Couldn't find handler for %2")
            .arg(inputid).arg(devname));
    }

    s_hlshandlers_refcnt.erase(rit);
    ref = nullptr;
}

HLSStreamHandler::HLSStreamHandler(const IPTVTuningData& tuning, int inputid)
    : IPTVStreamHandler(tuning, inputid)
{
    m_hls        = new HLSReader();
    m_readbuffer = new uint8_t[BUFFER_SIZE];
    m_throttle   = true;
}

HLSStreamHandler::~HLSStreamHandler(void)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "dtor");
    Stop();
    delete m_hls;
    delete[] m_readbuffer;
}

void HLSStreamHandler::run(void)
{
    RunProlog();

    QString url = m_tuning.GetURL(0).toString();
    int err_cnt = 0;
    int nil_cnt = 0;
    int open_sleep = 500;

    LOG(VB_GENERAL, LOG_INFO, LOC + "run() -- begin");

    SetRunning(true, false, false);

    if (!m_hls)
        return;
    m_hls->Throttle(false);

    int remainder = 0;
    while (_running_desired)
    {
        if (!m_hls->IsOpen(url))
        {
            if (!m_hls->Open(url, m_tuning.GetBitrate(0)))
            {
                if (m_hls->FatalError())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(open_sleep));
                if (open_sleep < 20000)
                    open_sleep += 500;
                continue;
            }
            open_sleep = 500;
            m_hls->Throttle(m_throttle);
            m_throttle = false;
        }

        int size = m_hls->Read(&m_readbuffer[remainder],
                               BUFFER_SIZE - remainder);

        size += remainder;

        if (size < 0)
        {
            // error
            if (++err_cnt > 10)
            {
                LOG(VB_RECORD, LOG_ERR, LOC + "HLSReader failed");
                Stop();
                break;
            }
            continue;
        }
        err_cnt = 0;

        if (size == 0)
        {
            if (nil_cnt < 4)
                ++nil_cnt;
            // range .25 to 1 second
            std::this_thread::sleep_for(std::chrono::milliseconds(nil_cnt *250));
            continue;
        }
        nil_cnt = 0;

        if (m_readbuffer[0] != 0x47)
        {
            LOG(VB_RECORD, LOG_INFO, LOC +
                QString("Packet not starting with SYNC Byte (got 0x%1)")
                .arg((char)m_readbuffer[0], 2, QLatin1Char('0')));
            continue;
        }

        {
            QMutexLocker locker(&_listener_lock);
            HLSStreamHandler::StreamDataList::const_iterator sit;
            sit = _stream_data_list.begin();
            for (; sit != _stream_data_list.end(); ++sit)
                remainder = sit.key()->ProcessData(m_readbuffer, size);
        }

        if (remainder > 0)
        {
            if (size > remainder) // unprocessed bytes
                memmove(m_readbuffer, &(m_readbuffer[size - remainder]),
                        remainder);

            LOG(VB_RECORD, LOG_INFO, LOC +
                QString("data_length = %1 remainder = %2")
                .arg(size).arg(remainder));
        }

        if (m_hls->IsThrottled())
            std::this_thread::sleep_for(std::chrono::seconds(1));
        else if (size < BUFFER_SIZE)
        {
            LOG(VB_RECORD, LOG_DEBUG, LOC +
                QString("Requested %1 bytes, got %2 bytes.")
                .arg(BUFFER_SIZE).arg(size));
            // hundredth of a second.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    m_hls->Throttle(false);

    SetRunning(false, false, false);
    RunEpilog();

    LOG(VB_GENERAL, LOG_INFO, LOC + "run() -- done");
}
