#include <jni.h>
#include <string>
#include "bytehook.h"
#include "xunwind.h"
#include <unistd.h>
#include <android/log.h>
#include <sstream>
#include <sstream>
#include <cxxabi.h>
#include "Backtrace.h"
#include "QuickenMaps.h"
#include "LocalMaps.h"
#include "DebugDexFiles.h"
#include "third_party/wechat/libunwindstack/deps/android-base/include/android-base/stringprintf.h"

#define LOG_TAG "IO_MONITOR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define SAMPLE_LOG_TAG        "xunwind_tag"
#define SAMPLE_LOG_PRIORITY   ANDROID_LOG_INFO
// 原始函数声明
static int (*original_open)(const char*, int, int);
static ssize_t (*original_read)(int, void*, size_t);
static ssize_t (*original_write)(int, const void*, size_t);
static int (*original_close)(int);

const static char* TARGET_MODULES[] = {
        "libopenjdkjvm.so",
        "libjavacore.so",
        "libopenjdk.so"
};
const static size_t TARGET_MODULE_COUNT = sizeof(TARGET_MODULES) / sizeof(char*);
JavaVM* gJavaVM; // 通常在 JNI_OnLoad 中初始化
jclass clazz;
jboolean enable_print_log;

JNIEnv* GetJNIEnv() {
    JNIEnv* env = nullptr;
    if (gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        gJavaVM->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

#define FRAME_MAX_SIZE 128
void dwarf_format_frame(const unwindstack::FrameData &frame, unwindstack::MapInfo *map_info,
                        unwindstack::Elf *elf, bool is32Bit, std::string &data) {
    if (is32Bit) {
        data += android::base::StringPrintf("  #%02zu pc %08" PRIx64, frame.num,
                (uint64_t) frame.rel_pc);
    } else {
        data += android::base::StringPrintf("  #%02zu pc %016" PRIx64, frame.num,
                (uint64_t) frame.rel_pc);
    }

    if (frame.map_start == frame.map_end) {
        // No valid map associated with this frame.
        data += "  <unknown>";
    } else if (elf != nullptr && !elf->GetSoname().empty()) {
        data += "  " + elf->GetSoname();
    } else if (!map_info->name.empty()) {
        data += "  " + map_info->name;
    } else {
        data += android::base::StringPrintf("  <anonymous:%" PRIx64 ">", frame.map_start);
    }

    if (frame.map_elf_start_offset != 0) {
        data += android::base::StringPrintf(" (offset 0x%" PRIx64 ")", frame.map_elf_start_offset);
    }

    if (!frame.function_name.empty()) {
        char *demangled_name = abi::__cxa_demangle(frame.function_name.c_str(), nullptr, nullptr,
                                                   nullptr);
        if (demangled_name == nullptr) {
            data += " (" + frame.function_name;
        } else {
            data += " (";
            data += demangled_name;
            free(demangled_name);
        }
        if (frame.function_offset != 0) {
            data += android::base::StringPrintf("+%" PRId64, frame.function_offset);
        }
        data += ')';
    }

    if (map_info != nullptr) {
        std::string build_id = map_info->GetPrintableBuildID();
        if (!build_id.empty()) {
            data += " (BuildId: " + build_id + ')';
        }
    }
}
void print_dwarf_unwind() {
    std::vector<unwindstack::FrameData> frames;
    std::shared_ptr<unwindstack::Regs> regs(unwindstack::Regs::CreateFromLocal());
    unwindstack::RegsGetLocal(regs.get());

    wechat_backtrace::BACKTRACE_FUNC_WRAPPER(dwarf_unwind)(regs.get(), frames, FRAME_MAX_SIZE);



    std::shared_ptr<wechat_backtrace::Maps> quicken_maps = wechat_backtrace::Maps::current();
    if (!quicken_maps) {
        LOGE("Err: unable to get maps.");
        return;
    }
    auto process_memory = unwindstack::Memory::CreateProcessMemory(getpid());
    auto jit_debug = wechat_backtrace::DebugJit::Instance();
    auto dex_debug = wechat_backtrace::DebugDexFiles::Instance();
    size_t num = 0;
    for (auto p_frame = frames.begin(); p_frame != frames.end(); ++p_frame, num++) {

        unwindstack::MapInfo *map_info = quicken_maps->Find(p_frame->pc);
        unwindstack::Elf *elf = nullptr;
        if (map_info) {

            if (p_frame->is_dex_pc) {
                dex_debug->GetMethodInformation(quicken_maps.get(), map_info, p_frame->pc,
                                                &p_frame->function_name,
                                                &p_frame->function_offset);
            } else {

                elf = map_info->GetElf(process_memory, regs->Arch());
                elf->GetFunctionName(p_frame->rel_pc, &p_frame->function_name,
                                     &p_frame->function_offset);
                if (!elf->valid()) {
                    unwindstack::Elf *jit_elf = jit_debug->GetElf(quicken_maps.get(), p_frame->pc);
                    if (jit_elf) {
                        jit_elf->GetFunctionName(p_frame->pc, &p_frame->function_name,
                                                 &p_frame->function_offset);
                    }
                }
            }
        }
        std::string formatted;
        dwarf_format_frame(*p_frame, map_info, elf, regs->Arch() == unwindstack::ARCH_ARM,
                           formatted);

        LOGI("%s", formatted.c_str());
    }
}

void saveBackTrace(int fd, char *log) {
    JNIEnv* mEnv = GetJNIEnv();
    jmethodID methodId = mEnv->GetStaticMethodID(clazz, "saveBackTrace", "(ILjava/lang/String;)V");
    // 创建 Java 字符串
    jstring str = mEnv->NewStringUTF(log);
    // 调用方法
    mEnv->CallStaticVoidMethod(clazz, methodId, fd, str);
    // 释放局部引用
    mEnv->DeleteLocalRef(str);
}

void removeBackTrace(int fd) {
    JNIEnv* mEnv = GetJNIEnv();
    jmethodID methodId = mEnv->GetStaticMethodID(clazz, "removeBackTrace", "(I)V");
    // 调用方法
    mEnv->CallStaticVoidMethod(clazz, methodId, fd);
}

void print_lines(const std::string& str) {
    std::stringstream ss(str);
    std::string line;
    while (std::getline(ss, line)) {
        LOGI("%s", line.c_str());
    }
}
void print_open_strace(int fd) {
    print_dwarf_unwind();
    pid_t pid = getpid();
    pid_t tid = gettid();
    void *context = nullptr;
//    xunwind_cfi_log(pid, tid, context, SAMPLE_LOG_TAG,
//                    SAMPLE_LOG_PRIORITY, NULL);
    char * log = xunwind_cfi_get(pid, tid, context, NULL);
    if (log == nullptr) {
        LOGI("strace = null, return");
        return;
    }
    int length = strlen(log);
    LOGI("log length: %d", length);
    saveBackTrace(fd, log);
    if (enable_print_log) {
        print_lines(log);
    }
    free(log);
}

void print_close_strace(int fd) {
    print_dwarf_unwind();
    pid_t pid = getpid();
    pid_t tid = gettid();
    void *context = nullptr;
//    xunwind_cfi_log(pid, tid, context, SAMPLE_LOG_TAG,
//                    SAMPLE_LOG_PRIORITY, NULL);
    char * log = xunwind_cfi_get(pid, tid, context, NULL);
    if (log == nullptr) {
        LOGI("strace = null, return");
        return;
    }
    int length = strlen(log);
    LOGI("log length: %d", length);
    removeBackTrace(fd);
    if (enable_print_log) {
        print_lines(log);
    }
    free(log);
}

//---------------- Hook 函数 -----------------
int hooked_open(const char* pathname, int flags, int mode) {
    LOGI("HOOKED open: %s", pathname);
    BYTEHOOK_STACK_SCOPE();
    // 调用原始函数
    int ret = BYTEHOOK_CALL_PREV(*original_open, pathname, flags, mode);
    // 记录堆栈
    print_open_strace(ret);
    LOGI("HOOKED open ret: %d", ret);
    return ret;
}

int hooked_close(int fd) {
    LOGI("HOOKED close: %d", fd);
    BYTEHOOK_STACK_SCOPE();
    // 调用原始函数
    int ret = BYTEHOOK_CALL_PREV(*original_close, fd);
    LOGI("HOOKED close ret: %d", ret);
    // 记录堆栈
    print_close_strace(fd);
    return ret;
}

//---------------- Hook 初始化 -----------------
__attribute__((constructor)) void init_hook() {
    for (int i = 0; i < TARGET_MODULE_COUNT; ++i) {
        const char *so_name = TARGET_MODULES[i];
        LOGI("HOOKED so: %s", so_name);
        bytehook_stub_t stub_open = bytehook_hook_single(
                so_name,
                NULL,
                "open",
                (void*)hooked_open,
                NULL,
                NULL);
        if (stub_open == NULL) LOGE("Failed to hook open");
        bytehook_stub_t stub_close = bytehook_hook_single(
                so_name,
                NULL,
                "close",
                (void*)hooked_close,
                NULL,
                NULL);
        if (stub_close == NULL) LOGE("Failed to hook close");
    }
    LOGI("Hooks initialized");
}



extern "C" JNIEXPORT void JNICALL
Java_com_hook_fdmonitor_FdMonitorManager_initMonitor(
        JNIEnv* env,
        jclass jclazz, jboolean enablePrintLog) {
    print_dwarf_unwind();
    enable_print_log = enablePrintLog;
    init_hook();
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // 版本不兼容
    }
    // 保存 JavaVM 全局引用
    gJavaVM = vm;
    jclass jclazz = env->FindClass("com/hook/fdmonitor/FdMonitorManager");
    if(jclazz == nullptr){
        LOGI("clazz = nullptr");
    }
    clazz = static_cast<jclass>(env->NewGlobalRef(jclazz));
    return JNI_VERSION_1_6; // 返回支持的 JNI 版本
}

