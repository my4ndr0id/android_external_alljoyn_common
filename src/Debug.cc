/**
 * @file
 *
 * This file implements the debug print functions used by the debug print macros.
 */

/******************************************************************************
 *
 *
 * Copyright 2009-2011, Qualcomm Innovation Center, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 ******************************************************************************/

#include <qcc/platform.h>

#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <map>
#include <stdarg.h>
#include <stdio.h>

#include <qcc/Debug.h>
#include <qcc/Environ.h>
#include <qcc/Mutex.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/Thread.h>
#include <qcc/time.h>


using namespace std;
using namespace qcc;

#define QCC_MODULE "DEBUG"

#undef min
#undef max

class StdoutLock {
  public:
    StdoutLock() { }

    ~StdoutLock()
    {
        m_destructed = true;
        delete m_mutex;
        m_mutex = 0;
    }

    bool Lock(void)
    {
        Mutex* mutex = Get();
        if (mutex) {
            mutex->Lock();
            return true;
        }
        return false;
    }

    void Unlock(void)
    {
        Mutex* mutex = Get();
        if (mutex) {
            mutex->Unlock();
        }
    }

  private:
    Mutex* Get(void)
    {
        if (m_mutex == NULL && m_destructed == false) {
            /* Be sure to use the non-debug-printing version of Mutex to avoid infinite recursion */
            m_mutex = new Mutex();
        }
        return m_mutex;
    }

    static Mutex* m_mutex;
    static bool m_destructed;
};

Mutex* StdoutLock::m_mutex = NULL;
bool StdoutLock::m_destructed = false;

StdoutLock stdoutLock;

int QCC_SyncPrintf(const char* fmt, ...)
{
    int ret = 0;
    va_list ap;

    va_start(ap, fmt);
    if (stdoutLock.Lock()) {
        ret = vprintf(fmt, ap);
        stdoutLock.Unlock();
    }
    va_end(ap);

    return ret;
}


static void WriteMsg(DbgMsgType type, const char* module, const char* msg, void* context)
{
    bool ret = false;
    FILE* file = reinterpret_cast<FILE*>(context);

    fflush(stdout);     // Helps make output cleaner on Windows.
    ret = stdoutLock.Lock();

    if (ret) {
        fputs(msg, file);
        stdoutLock.Unlock();
    }
}


class DebugControl {
  private:
    static DebugControl* self;

    Mutex mutex;
    QCC_DbgMsgCallback cb;
    void* context;
    uint32_t allLevel;
    map<const qcc::String, uint32_t> modLevels;
    bool printThread;

    DebugControl(void) : cb(WriteMsg), context(stderr), allLevel(0), printThread(true)
    {
        Init();
    }

  public:

    static DebugControl* GetDebugControl(void)
    {
        if (self == NULL) {
            self = new DebugControl();
        }
        return self;
    }

    void Init(void)
    {
        Environ* env = Environ::GetAppEnviron();
        Environ::const_iterator iter;
        static const char varPrefix[] = "ER_DEBUG_";
        static const size_t varPrefixLen = sizeof(varPrefix) - 1;
        env->Preload(varPrefix);

        for (iter = env->Begin(); iter != env->End(); ++iter) {
            qcc::String var(iter->first);
            if (var.compare("ER_DEBUG_THREADNAME") == 0) {
                printThread = ((iter->second.compare("0") != 0) &&
                               (iter->second.compare("off") != 0) &&
                               (iter->second.compare("OFF") != 0));
            } else if (var.compare(0, varPrefixLen, varPrefix) == 0) {
                uint32_t level = StringToU32(iter->second, 0, 0);
                if (var.compare("ER_DEBUG_ALL") == 0) {
                    allLevel = level;
                } else {
                    modLevels.insert(pair<const qcc::String, int>(var.substr(varPrefixLen), level));
                }
            }
        }
    }

    void AddTagLevelPair(const char* tag, uint32_t level)
    {
        modLevels.insert(pair<const qcc::String, uint32_t>(tag, level));
    }

    void SetAllLevel(uint32_t level)
    {
        allLevel = level;
    }

    void WriteDebugMessage(DbgMsgType type, const char* module, const qcc::String msg)
    {
        mutex.Lock();
        cb(type, module, msg.c_str(), context);
        mutex.Unlock();
    }

    void Register(QCC_DbgMsgCallback cb, void* context)
    {
        assert(cb != NULL);
        this->context = context;
        this->cb = cb;
    }

    bool Check(DbgMsgType type, const char* module);

    bool PrintThread() const { return printThread; }
};

DebugControl* DebugControl::self = NULL;

bool DebugControl::Check(DbgMsgType type, const char* module)
{
    map<const qcc::String, uint32_t>::const_iterator iter;
    uint32_t level;
    iter = modLevels.find(module);
    if (iter == modLevels.end()) {
        level = allLevel;
    } else {
        level = iter->second;
    }

    switch (type) {
    case DBG_LOCAL_ERROR:
    case DBG_REMOTE_ERROR:
        return true;    // Always print errors.

    case DBG_HIGH_LEVEL:
        return (level & 0x1);

    case DBG_GEN_MESSAGE:
        return (level & 0x2);

    case DBG_API_TRACE:
        return (level & 0x4);

    case DBG_REMOTE_DATA:
    case DBG_LOCAL_DATA:
        return (level & 0x8);

    }

    return false;  // Should never get here.
}


static const char* Type2Str(DbgMsgType type)
{
    const char* typeStr;

    switch (type) {
    case DBG_LOCAL_ERROR:
        typeStr = "****** ERROR";
        break;

    case DBG_REMOTE_ERROR:
        typeStr = "REMOTE_ERROR";
        break;

    case DBG_GEN_MESSAGE:
        typeStr = "DEBUG";
        break;

    case DBG_API_TRACE:
        typeStr = "TRACE";
        break;

    case DBG_HIGH_LEVEL:
        typeStr = "HL_DBG";
        break;

    case DBG_REMOTE_DATA:
        typeStr = "REM_DATA";
        break;

    case DBG_LOCAL_DATA:
        typeStr = "LOC_DATA";
        break;

    default:
        typeStr = "";
    }

    return typeStr;
}


static void GenPrefix(qcc::String& oss, DbgMsgType type, const char* module, const char* filename, int lineno, bool printThread)
{
    uint32_t timestamp = GetTimestamp();
    static const size_t timeTypeWidth = 18;
    static const size_t moduleWidth = 12;
    static const size_t threadWidth = 18;
    static const size_t bonusWidth = 8;
    static const size_t fileLineWidth = 32;
    size_t colStop = timeTypeWidth;

    oss.reserve(timeTypeWidth + moduleWidth + threadWidth + fileLineWidth + oss.capacity());

    // Timestamp - col 0
    oss.append(U32ToString((timestamp / 1000) % 10000, 10, 4, ' '));
    oss.push_back('.');
    oss.append(U32ToString(timestamp % 1000, 10, 3, '0'));
    oss.push_back(' ');

    // Output type - col 9
    oss.append(Type2Str(type));
    do {
        oss.push_back(' ');
    } while (oss.size() < colStop);

    // Subsystem module - col 18
    colStop += moduleWidth;
    oss.append(module);
    do {
        oss.push_back(' ');
    } while (oss.size() < colStop);

    if (printThread) {
        // Thread name - col 30
        colStop += threadWidth;
        oss.append(Thread::GetThreadName());
        do {
            oss.push_back(' ');
        } while (oss.size() < colStop);
    } else {
        // Extra space for file name
        colStop += bonusWidth;
    }

    // File name - col 30 or 48
    colStop += fileLineWidth;
    size_t fnSize = strlen(filename);
    qcc::String line = U32ToString(lineno, 10);
    size_t fileWidth = colStop - (oss.size() + line.size() + 4);  // Figure out how much room for filename
    if (fnSize > fileWidth) {
        // Filename is too long so chop off the first part (which should just be leading directories).
        oss.append("...");
        oss.append(filename + (fnSize - (fileWidth - 3)));
    } else {
        oss.append(filename);
    }
    oss.push_back(':');
    oss.append(line);
    do {
        oss.push_back(' ');
    } while (oss.size() < (colStop - 2));

    oss.append("| ");

    // Msg at col 70 or 80
}


class DebugContext {
  private:
    char msg[2000];  // Just allocate a buffer that's 'big enough'.
    size_t msgLen;

  public:
    DebugContext(void) : msgLen(0)
    {
        msg[0] = '\0';
    }

    ~DebugContext(void)
    {
    }

    void Process(DbgMsgType type, const char* module, const char* filename, int lineno);
    void Vprintf(const char* fmt, va_list ap);
};

void DebugContext::Process(DbgMsgType type, const char* module, const char* filename, int lineno)
{
    DebugControl* control = DebugControl::GetDebugControl();
    qcc::String oss;

    oss.reserve(sizeof(msg));

    GenPrefix(oss, type, module, filename, lineno, control->PrintThread());

    if (msg != NULL) {
        oss.append(msg);
    }

    oss.push_back('\n');

    control->WriteDebugMessage(type, module, oss);
}

void DebugContext::Vprintf(const char* fmt, va_list ap)
{
    int mlen;

    if (stdoutLock.Lock()) {
        if (msgLen < sizeof(msg)) {
            mlen = vsnprintf(msg + msgLen, sizeof(msg) - msgLen, fmt, ap);

            if (mlen > 0) {
                msgLen += mlen;
                if (msgLen > sizeof(msg)) {
                    msgLen = sizeof(msg);
                }
            }
        }
        stdoutLock.Unlock();
    }
}


void QCC_InitializeDebugControl(void)
{
    DebugControl* dbg = DebugControl::GetDebugControl();
    dbg->Init();
}

void* _QCC_DbgPrintContext(const char* fmt, ...)
{
    DebugContext* context = new DebugContext();
    va_list ap;

    va_start(ap, fmt);
    context->Vprintf(fmt, ap);
    va_end(ap);
    return context;
}


void _QCC_DbgPrintAppend(void* ctx, const char* fmt, ...)
{
    DebugContext* context = reinterpret_cast<DebugContext*>(ctx);
    va_list ap;

    va_start(ap, fmt);
    context->Vprintf(fmt, ap);
    va_end(ap);
}


void _QCC_DbgPrintProcess(void* ctx, DbgMsgType type, const char* module, const char* filename, int lineno)
{
    DebugContext* context = reinterpret_cast<DebugContext*>(ctx);
    context->Process(type, module, filename, lineno);
    delete context;
}

void _QCC_LogError(QStatus status, const char* filename, int lineno)
{
    DebugContext* ctx = reinterpret_cast<DebugContext*>(_QCC_DbgPrintContext(" 0x%04x", status));
    _QCC_DbgPrintProcess(ctx, DBG_LOCAL_ERROR, "", filename, lineno);
}

void QCC_RegisterOutputCallback(QCC_DbgMsgCallback cb, void* context)
{
    DebugControl* dbg = DebugControl::GetDebugControl();

    dbg->Register(cb, context);
}

void QCC_RegisterOutputFile(FILE* file)
{
    DebugControl* dbg = DebugControl::GetDebugControl();

    dbg->Register(WriteMsg, reinterpret_cast<void*>(file));
}


int _QCC_DbgPrintCheck(DbgMsgType type, const char* module)
{
    DebugControl* control = DebugControl::GetDebugControl();

    return static_cast<int>(control->Check(type, module));
}


void _QCC_DbgDumpHex(DbgMsgType type, const char* module, const char* filename, int lineno,
                     const char* dataStr, const void* data, size_t dataLen)
{
    if (_QCC_DbgPrintCheck(type, module)) {
        if (data == NULL) {
            DebugContext* context = new DebugContext();
            _QCC_DbgPrintAppend(context, "<null>");
            _QCC_DbgPrintProcess(context, type, module, filename, lineno);
        } else {
            DebugContext ctx;
            DebugControl* control = DebugControl::GetDebugControl();
            const uint8_t* pos(reinterpret_cast<const uint8_t*>(data));
            static const size_t LINE_LEN = 16;
            size_t i;
            qcc::String oss;

            oss.reserve(strlen(dataStr) + 8 + dataLen * 4 + (((dataLen + 15) / 16) * (40 + strlen(module))));

            GenPrefix(oss, type, module, filename, lineno, control->PrintThread());

            oss.append(dataStr);
            oss.push_back('[');
            oss.append(U32ToString(dataLen, 16, 4, '0'));
            oss.append("]:\n");

            while (dataLen > 0) {
                size_t dumpLen = (std::min)(dataLen, LINE_LEN);

                oss.append("         ");
                oss.append(Type2Str(type));
                oss.push_back(' ');
                oss.append(module);
                oss.append("    ");
                oss.append(U32ToString(pos - reinterpret_cast<const uint8_t*>(data), 16, 4, '0'));
                oss.append(" | ");

                for (i = 0; i < LINE_LEN; ++i) {
                    if (i == (LINE_LEN / 2)) {
                        oss.append("- ");
                    }
                    if (i < dumpLen) {
                        oss.append(U32ToString(static_cast<uint32_t>(pos[i]), 16, 2, '0'));
                        oss.push_back(' ');
                    } else {
                        oss.append("   ");
                    }
                }

                oss.append(" |  ");

                for (i = 0; i < LINE_LEN; ++i) {
                    if (i == (LINE_LEN / 2)) {
                        oss.append(" - ");
                    }
                    if (i < dumpLen) {
                        oss.push_back(isprint(pos[i]) ? pos[i] : '.');
                    } else {
                        oss.push_back(' ');
                    }
                }

                oss.push_back('\n');

                pos += dumpLen;
                dataLen -= dumpLen;
            }
            control->WriteDebugMessage(type, module, oss);
        }
    }
}

void QCC_SetDebugLevel(const char* module, uint32_t level)
{
    DebugControl* control = DebugControl::GetDebugControl();
    if (strcmp(module, "ALL") == 0) {
        control->SetAllLevel(level);
    } else {
        control->AddTagLevelPair(module, level);
    }
}

void QCC_SetLogLevels(const char* logEnv)
{
    size_t pos = 0;
    qcc::String s = logEnv;
    while (qcc::String::npos != pos) {
        size_t eqPos = s.find_first_of('=', pos);
        size_t endPos = (qcc::String::npos == eqPos) ? eqPos : s.find_first_of(';', eqPos);
        if (qcc::String::npos != eqPos) {
            qcc::String tag = s.substr(pos, eqPos - pos);
            qcc::String levelStr = (qcc::String::npos == endPos) ? s.substr(eqPos + 1) : s.substr(eqPos + 1, endPos - eqPos - 1);
            uint32_t level = StringToU32(levelStr, 0, 0);
            QCC_SetDebugLevel(tag.c_str(), level);
        }
        pos = (qcc::String::npos == endPos) ? endPos : endPos + 1;
    }
}

#ifdef QCC_OS_ANDROID

#include <android/log.h>

static void AndroidLogCB(DbgMsgType type, const char* module, const char* msg, void* context)
{
    int priority;
    switch (type) {
    case DBG_LOCAL_ERROR:
    case DBG_REMOTE_ERROR:
        priority = ANDROID_LOG_ERROR;
        break;

    case DBG_HIGH_LEVEL:
        priority = ANDROID_LOG_WARN;
        break;

    case DBG_GEN_MESSAGE:
        priority = ANDROID_LOG_INFO;
        break;

    case DBG_API_TRACE:
        priority = ANDROID_LOG_DEBUG;
        break;

    default:
    case DBG_REMOTE_DATA:
    case DBG_LOCAL_DATA:
        priority = ANDROID_LOG_VERBOSE;
        break;
    }
    __android_log_write(priority, module, msg);
}

#endif

#ifdef QCC_OS_WINDOWS

static void WindowsLogCB(DbgMsgType type, const char* module, const char* msg, void* context)
{
    OutputDebugStringA(msg);
}

#endif

void QCC_UseOSLogging(bool useOSLog)
{
#if defined(QCC_OS_ANDROID)
    QCC_DbgMsgCallback cb = useOSLog ? AndroidLogCB : WriteMsg;
    void* context = stderr;
#elif defined(QCC_OS_WINDOWS)
    QCC_DbgMsgCallback cb = useOSLog ? WindowsLogCB : WriteMsg;
    void* context = stderr;
#else
    QCC_DbgMsgCallback cb = WriteMsg;
    void* context = stderr;
#endif
    QCC_RegisterOutputCallback(cb, context);
}

