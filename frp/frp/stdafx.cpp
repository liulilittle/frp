#include <frp/stdafx.h>
#include <frp/Random.h>
#include <frp/io/File.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/file.h>
#endif

namespace frp {
    static Random GLOBAL_RANDOBJECT;

    void SetThreadPriorityToMaxLevel() noexcept {
#ifdef _WIN32
        SetThreadPriority(GetCurrentProcess(), THREAD_PRIORITY_TIME_CRITICAL);
#else
        /* ps -eo state,uid,pid,ppid,rtprio,time,comm */
        struct sched_param param_;
        param_.sched_priority = sched_get_priority_max(SCHED_FIFO); // SCHED_RR
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &param_);
#endif
    }

    void SetProcessPriorityToMaxLevel() noexcept {
#ifdef _WIN32
        SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
#else
        char path_[260];
        snprintf(path_, sizeof(path_), "/proc/%d/oom_adj", getpid());

        char level_[] = "-17";
        frp::io::File::WriteAllBytes(path_, level_, sizeof(level_));

        /* Processo pai deve ter prioridade maior que os filhos. */
        setpriority(PRIO_PROCESS, 0, -20);

        /* ps -eo state,uid,pid,ppid,rtprio,time,comm */
        struct sched_param param_;
        param_.sched_priority = sched_get_priority_max(SCHED_FIFO); // SCHED_RR
        sched_setscheduler(getpid(), SCHED_RR, &param_);
#endif
    }

    bool ToBoolean(const char* s) noexcept {
        if (NULL == s || *s == '\x0') {
            return false;
        }

        char ch = s[0];
        if (ch == '0' || ch == ' ') {
            return false;
        }

        if (ch == 'f' || ch == 'F') {
            return false;
        }

        if (ch == 'n' || ch == 'N') {
            return false;
        }

        if (ch == 'c' || ch == 'C') {
            return false;
        }
        return true;
    }

    int GetHashCode(const char* s, int len) noexcept {
        if (s == NULL) {
            return 0;
        }

        if (len < 0) {
            len = (int)strlen((char*)s);
        }

        if (len < 1) {
            return 0;
        }

        static unsigned int qualityTable[] = {
             0x0, 0x77073096, 0xee0e612c, 0x990951ba, 0x76dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
             0xedb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x9b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
             0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
             0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
             0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
             0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
             0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
             0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
             0x76dc4190, 0x1db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x6b6b51f, 0x9fbfe4a5, 0xe8b8d433,
             0x7807c9a2, 0xf00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x86d3d2d, 0x91646c97, 0xe6635c01,
             0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
             0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
             0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
             0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
             0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
             0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
             0xedb88320, 0x9abfb3b6, 0x3b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x4db2615, 0x73dc1683,
             0xe3630b12, 0x94643b84, 0xd6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0xa00ae27, 0x7d079eb1,
             0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
             0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
             0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
             0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
             0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
             0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
             0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x26d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x5005713,
             0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0xcb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0xbdbdf21,
             0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
             0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
             0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
             0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
             0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
             0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
        };
        unsigned int hash = 0xFFFFFFFF;
        unsigned char* buf = (unsigned char*)s;

        for (int i = 0; i < len; i++) {
            hash = ((hash >> 8) & 0x00FFFFFF) ^ qualityTable[(hash ^ buf[i]) & 0xFF];
        }

        unsigned int num = hash ^ 0xFFFFFFFF;
        return (int)num;
    }

    Char RandomAscii() noexcept {
        static const int m_ = 3;
        static Byte x_[m_] = { 'a', 'A', '0' };
        static Byte y_[m_] = { 'z', 'Z', '9' };

        int i_ = abs(GLOBAL_RANDOBJECT.Next()) % m_;
        return (Char)GLOBAL_RANDOBJECT.Next(x_[i_], y_[i_]);
    }

    int RandomNext() noexcept {
        return GLOBAL_RANDOBJECT.Next(0, INT_MAX);
    }

    int RandomNext(int minValue, int maxValue) noexcept {
        return GLOBAL_RANDOBJECT.Next(minValue, maxValue);
    }

    double RandomNextDouble() noexcept {
        return GLOBAL_RANDOBJECT.NextDouble();
    }

    std::string StrFormatByteSize(Int64 size) noexcept {
        static const char* aszByteUnitsNames[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB", "DB", "NB" };

        long double d = llabs(size);
        unsigned int i = 0;
        while (i < 10 && d > 1024) {
            d /= 1024;
            i++;
        }

        char sz[1000 + 1];
        snprintf(sz, 1000, "%Lf %s", d, aszByteUnitsNames[i]);
        return sz;
    }

    bool GetCommandArgument(const char* name, int argc, const char** argv, bool defaultValue) noexcept {
        std::string str = GetCommandArgument(name, argc, argv);
        if (str.empty()) {
            return defaultValue;
        }
        return ToBoolean(str.data());
    }

    std::string GetCommandArgument(const char* name, int argc, const char** argv, const char* defaultValue) noexcept {
        std::string defValue;
        if (defaultValue) {
            defValue = defaultValue;
        }
        return GetCommandArgument(name, argc, argv, defValue);
    }

    std::string GetCommandArgument(const char* name, int argc, const char** argv, const std::string& defaultValue) noexcept {
        std::string str = GetCommandArgument(name, argc, argv);
        return str.empty() ? defaultValue : str;
    }

    bool IsInputHelpCommand(int argc, const char* argv[]) noexcept {
        const int HELP_COMMAND_COUNT = 4;
        const char* HELP_COMMAND_LIST[HELP_COMMAND_COUNT] = {
            "-h",
            "--h",
            "-help",
            "--help"
        };
        for (int i = 1; i < argc; i++) {
            std::string line = argv[i];
            if (line.empty()) {
                continue;
            }

            line = ToLower(line);
            for (int j = 0; j < HELP_COMMAND_COUNT; j++) {
                std::string key = HELP_COMMAND_LIST[j];
                if (line == key) {
                    return true;
                }

                std::size_t pos = line.find(key + "=");
                if (pos != std::string::npos) {
                    return true;
                }

                pos = line.find(key + " ");
                if (pos != std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    }

    std::string GetCommandArgument(const char* name, int argc, const char** argv) noexcept {
        if (NULL == name || argc <= 1) {
            return "";
        }

        std::string key1 = name;
        if (key1.empty()) {
            return "";
        }

        std::string key2 = key1 + " ";
        key1.append("=");
        
        std::string line;
        for (int i = 1; i < argc; i++) {
            line.append(RTrim(LTrim<std::string>(argv[i])));
            line.append(" ");
        }
        if (line.empty()) {
            return "";
        }

        std::string* key = addressof(key1);
        std::size_t L = line.find(*key);
        if (L == std::string::npos) {
            key = addressof(key2);
            L = line.find(*key);
            if (L == std::string::npos) {
                return "";
            }
        }

        if (L) {
            char ch = line[L - 1];
            if (ch != ' ') {
                return "";
            }
        }

        std::string cmd;
        std::size_t M = L + key->size();
        std::size_t R = line.find(' ', L);
        if (M >= R) {
            R = std::string::npos;
            for (std::size_t I = M, SZ = line.size(); I < SZ; I++) {
                int ch = line[I];
                if (ch == ' ') {
                    R = I;
                    L = M;
                    break;
                }
            }
            if (!L || L == std::string::npos) {
                return "";
            }
        }

        if (R == std::string::npos) {
            if (M != line.size()) {
                cmd = line.substr(M);
            }
        }
        else {
            int S = (int)(R - M);
            if (S > 0) {
                cmd = line.substr(M, S);
            }
        }
        return cmd;
    }

    std::string GetFullExecutionFilePath() noexcept {
#ifdef _WIN32
        char exe[8096];
        GetModuleFileNameA(NULL, exe, sizeof(exe));
        return exe;
#else
        char sz[260 + 1];
        int dw = readlink("/proc/self/exe", sz, 260);
        sz[dw] = '\x0';
        return dw < 1 ? "" : std::string(sz, dw);
#endif
    }

    std::string GetCurrentDirectoryPath() noexcept {
#ifdef _WIN32
        char cwd[8096];
        GetCurrentDirectoryA(sizeof(cwd), cwd);
        return cwd;
#else
        char sz[260 + 1];
        return getcwd(sz, 260);
#endif
    }

    std::string GetApplicationStartupPath() noexcept {
        std::string exe = GetFullExecutionFilePath();
#ifdef _WIN32
        std::size_t pos = exe.rfind('\\');
#else
        std::size_t pos = exe.rfind('/');
#endif
        if (pos == std::string::npos) {
            return exe;
        }
        else {
            return exe.substr(0, pos);
        }
    }

    std::string GetExecutionFileName() noexcept {
        std::string exe = GetFullExecutionFilePath();
#ifdef _WIN32
        std::size_t pos = exe.rfind('\\');
#else
        std::size_t pos = exe.rfind('/');
#endif
        if (pos == std::string::npos) {
            return exe;
        }
        else {
            return exe.substr(pos + 1);
        }
    }

    int GetCurrentProcessId() noexcept {
#if defined(_WIN32) || defined(_WIN64)
        return ::GetCurrentProcessId();
#else
        return ::getpid();
#endif
    }

    int GetProcesserCount() noexcept {
        int count = 0;
#if defined(_WIN32) || defined(_WIN64)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        count = si.dwNumberOfProcessors;
#else
#if !defined(ANDROID) || __ANDROID_API__ >= 23
        count = get_nprocs();
#else
        count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
#endif
        if (count < 1) {
            count = 1;
        }
        return count;
    }

    boost::uuids::uuid GuidGenerate() noexcept {
        boost::uuids::random_generator rgen;
        return rgen();
    }

    std::string LexicalCast(const boost::uuids::uuid& uuid) noexcept {
        return boost::lexical_cast<std::string>(uuid);;
    }

    std::string GuidToString(const boost::uuids::uuid& uuid) noexcept {
        return boost::uuids::to_string(uuid);
    }

    boost::uuids::uuid LexicalCast(const void* guid, int length) noexcept {
        boost::uuids::uuid uuid;
        if (NULL == guid) {
            length = 0;
        }

        const int max_length = sizeof(uuid.data);
        if (length >= max_length) {
            memcpy(uuid.data, guid, length);
        }
        elif(length > 0) {
            memcpy(uuid.data, guid, length);
            memset(uuid.data + length, 0, max_length - length);
        }
        else {
            memset(uuid.data, 0, sizeof(uuid.data));
        }
        return uuid;
    }

    boost::uuids::uuid StringToGuid(const std::string& guid) noexcept {
        boost::uuids::string_generator sgen;
        try {
            return sgen(guid);
        }
        catch (std::exception&) {
            return LexicalCast(NULL, 0);
        }
    }

    const char* GetPlatformCode() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        return "x86_64";
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
        return "x86_32";
#elif defined(__ARM_ARCH_2__)
        return "ARM2";
#elif defined(__ARM_ARCH_3__) || defined(__ARM_ARCH_3M__)
        return "ARM3";
#elif defined(__ARM_ARCH_4T__) || defined(__TARGET_ARM_4T)
        return "ARM4T";
#elif defined(__ARM_ARCH_5_) || defined(__ARM_ARCH_5E_)
        return "ARM5"
#elif defined(__ARM_ARCH_6T2_) || defined(__ARM_ARCH_6T2_)
        return "ARM6T2";
#elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
        return "ARM6";
#elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
        return "ARM7";
#elif defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
        return "ARM7A";
#elif defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
        return "ARM7R";
#elif defined(__ARM_ARCH_7M__)
        return "ARM7M";
#elif defined(__ARM_ARCH_7S__)
        return "ARM7S";
#elif defined(__aarch64__) || defined(_M_ARM64)
        return "ARM64";
#elif defined(mips) || defined(__mips__) || defined(__mips)
        return "MIPS";
#elif defined(__sh__)
        return "SUPERH";
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
        return "POWERPC";
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
        return "POWERPC64";
#elif defined(__sparc__) || defined(__sparc)
        return "SPARC";
#elif defined(__m68k__)
        return "M68K";
#elif defined(__s390x__)
        return "S390X";
#else
        return "UNKNOWN";
#endif
    }
}