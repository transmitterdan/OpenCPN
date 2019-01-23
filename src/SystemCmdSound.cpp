/******************************************************************************
 *
 * Project:  OpenCPN
 *
 ***************************************************************************
 *   Copyright (C) 2013 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 */
#include <stdlib.h>
#include <thread>

#include <wx/file.h>
#include <wx/log.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "SystemCmdSound.h"

extern bool g_bquiting;     // Flag tells us O is shutting down

static int do_play(const char* cmd, const char* path)
{
    char buff[1024];
    snprintf(buff, sizeof(buff), cmd, path);

#ifdef _WIN32
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    // Launch external command string
    int status = CreateProcessA(NULL,   // No module name (use command line)
        buff,            // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        FALSE,          // Set handle inheritance to FALSE
        CREATE_NO_WINDOW,// No window flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi);           // Pointer to PROCESS_INFORMATION structure

    if (!status) {
        wxLogWarning("Cannot fork process running %s", buff);
        return -1;
    }
    // Here we wait a bit and check if the process is finished.
    int waitStatus = WaitForSingleObject(pi.hProcess, maxPlayTime);
    while (!g_bquiting && waitStatus == WAIT_TIMEOUT) {
        // if not finished wait a little bit longer
        waitStatus = WaitForSingleObject(pi.hProcess, maxPlayTime);
    }

    // if thread did not terminate naturally log it
    if (!g_bquiting && waitStatus != WAIT_OBJECT_0)
        wxLogWarning("Sound command produced unusual waitStatus %d", (int)waitStatus);

    if (!g_bquiting) {   // if shutting down let windows close down the sound process
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return 0;

#else

    int status = system(buff);
    if (status == -1) {
        wxLogWarning("Cannot fork process running %s", buff);
        return -1;
    }
    if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
        if (status != 0) {
            wxLogWarning("Exit code %d from command %s",
                status, buff);
        }
    } else {
        wxLogWarning("Strange return code %d (0x%x) running %s",
                     status, status, buff);
    }
    return status;

#endif /* _WIN32 */
}


bool SystemCmdSound::Load(const char* path, int deviceIndex)
{
    m_path = path;
#ifdef _DEBUG
    if (deviceIndex != -1) {
        wxLogWarning("Selecting device is not supported by SystemCmdSound");
    }
#endif /* _DEBUG */
    m_OK = wxFileExists(m_path);
    return m_OK;
}


bool SystemCmdSound::Stop(void)  { return false; }


bool SystemCmdSound::canPlay(void) 
{
    if (m_isPlaying)
        wxLogWarning("SystemCmdSound: cannot play: already playing");
    return m_OK && !m_isPlaying;
}


void SystemCmdSound::worker(void)
{
#ifdef _DEBUG
    wxLogMessage("SystemCmdSound::worker()");
#endif /* _DEBUG */
    m_isPlaying = true;
    do_play(m_cmd.c_str(), m_path.c_str());
    m_onFinished(m_callbackData);
    m_onFinished = 0;
    m_isPlaying = false;
}


bool SystemCmdSound::Play()
{
#ifdef _DEBUG
    wxLogInfo("SystemCmdSound::Play()");
#endif /* _DEBUG */
    if (m_isPlaying) {
        wxLogWarning("SystemCmdSound: cannot play: already playing");
        return false;
    }
    if  (m_onFinished) {
        std::thread t([this]() { worker(); });
        t.detach();
        return true;
    }
    int r = do_play(m_cmd.c_str(), m_path.c_str());
    return r == 0; 
}
