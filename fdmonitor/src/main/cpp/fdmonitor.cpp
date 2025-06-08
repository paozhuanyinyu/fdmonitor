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
static int (*original_android_fdsan_close_with_tag)(int, uint64_t);

const static char* TARGET_MODULES[] = {
        "libopenjdkjvm.so",
        "libjavacore.so",
        "libopenjdk.so"
};
const static size_t TARGET_MODULE_COUNT = sizeof(TARGET_MODULES) / sizeof(char*);
JavaVM* gJavaVM; // 通常在 JNI_OnLoad 中初始化
jclass clazz;
static jclass kJavaContextClass;
static jmethodID kMethodIDGetJavaContext;
static jfieldID kFieldIDStack;
static jfieldID kFieldIDThreadName;
jboolean enable_print_log;

JNIEnv* GetJNIEnv() {
    JNIEnv* env = nullptr;
    if (gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        gJavaVM->AttachCurrentThread(&env, nullptr);
    }
    return env;
}
void printTimestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_info);
    __android_log_print(ANDROID_LOG_INFO, "Timestamp", "%s.%03ld", buffer, ts.tv_nsec / 1000000);
}
#define FRAME_MAX_SIZE 32
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
    printTimestamp();
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
char *jstringToChars(JNIEnv *env, jstring jstr) {
    if (jstr == nullptr) {
        return nullptr;
    }

    jboolean isCopy = JNI_FALSE;
    const char *str = env->GetStringUTFChars(jstr, &isCopy);
    char *ret = strdup(str);
    env->ReleaseStringUTFChars(jstr, str);
    return ret;
}

void print_xunwind_log(int fd) {
    pid_t pid = getpid();
    pid_t tid = gettid();
    void *context = nullptr;
//    xunwind_cfi_log(pid, tid, context, SAMPLE_LOG_TAG,
//                    SAMPLE_LOG_PRIORITY, NULL);
    static uintptr_t g_frames[128];
    size_t frames_sz = xunwind_fp_unwind(g_frames, sizeof(g_frames) / sizeof(g_frames[0]),NULL);
    char * log = xunwind_frames_get(g_frames, frames_sz, "");
//    char * log = xunwind_cfi_get(pid, tid, context, NULL);
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
void print_open_strace(int fd) {
    JNIEnv* env = NULL;
    gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    jobject java_context_obj = env->CallStaticObjectMethod(clazz, kMethodIDGetJavaContext);
    if (NULL == java_context_obj) {
        LOGE("java_context_obj == NULL");
        return;
    }
    jstring j_stack = (jstring) env->GetObjectField(java_context_obj, kFieldIDStack);
    jstring j_thread_name = (jstring) env->GetObjectField(java_context_obj, kFieldIDThreadName);
    char* thread_name = jstringToChars(env, j_thread_name);
    char* stack = jstringToChars(env, j_stack);
    printTimestamp();
//    print_dwarf_unwind();
    print_xunwind_log(fd);
    LOGI("%s", stack);
    printTimestamp();

}



void print_close_strace(int fd) {
    JNIEnv* env = NULL;
    gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    jobject java_context_obj = env->CallStaticObjectMethod(clazz, kMethodIDGetJavaContext);
    if (NULL == java_context_obj) {
        LOGE("java_context_obj == NULL");
        return;
    }
    jstring j_stack = (jstring) env->GetObjectField(java_context_obj, kFieldIDStack);
    jstring j_thread_name = (jstring) env->GetObjectField(java_context_obj, kFieldIDThreadName);
    char* thread_name = jstringToChars(env, j_thread_name);
    char* stack = jstringToChars(env, j_stack);
    printTimestamp();
//    print_dwarf_unwind();
    print_xunwind_log(fd);
    LOGI("%s", stack);
    printTimestamp();
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
int hooked_android_fdsan_close_with_tag(int fd, uint64_t expected_tag) {
    LOGI("HOOKED android_fdsan_close_with_tag: %d", fd);
    BYTEHOOK_STACK_SCOPE();
    // 调用原始函数
    int ret = BYTEHOOK_CALL_PREV(*original_android_fdsan_close_with_tag, fd, expected_tag);
    LOGI("HOOKED close ret: %d", ret);
    // 记录堆栈
    print_close_strace(fd);
    return ret;
}

//---------------- Hook 初始化 -----------------
__attribute__((constructor)) void init_hook() {
    printTimestamp();
    print_dwarf_unwind();
    printTimestamp();
    bytehook_set_debug(true);
    for (int i = 0; i < TARGET_MODULE_COUNT; ++i) {
        const char *so_name = TARGET_MODULES[i];
        LOGI("HOOKED so: %s", so_name);
        bytehook_stub_t stub_open = bytehook_hook_single(
                so_name,
                NULL,
                "open",
                (int*)hooked_open,
                NULL,
                NULL);
        if (stub_open == NULL) LOGE("Failed to hook open");
        bytehook_stub_t stub_close = bytehook_hook_single(
                so_name,
                NULL,
                "close",
                (int*)hooked_close,
                NULL,
                NULL);
        if (stub_close == NULL) LOGE("Failed to hook close");
        bytehook_stub_t stub_android_fdsan_close_with_tag = bytehook_hook_single(
                so_name,
                NULL,
                "android_fdsan_close_with_tag",
                (int*) hooked_android_fdsan_close_with_tag,
                NULL,
                NULL);
        if (stub_android_fdsan_close_with_tag == NULL) LOGE("Failed to hook android_fdsan_close_with_tag");
    }
    LOGI("Hooks initialized");
}



extern "C" JNIEXPORT void JNICALL
Java_com_hook_fdmonitor_FdMonitorManager_initMonitor(
        JNIEnv* env,
        jclass jclazz, jboolean enablePrintLog) {
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
        LOGE("clazz = nullptr");
    }
    clazz = static_cast<jclass>(env->NewGlobalRef(jclazz));
    jclass temp_java_context_cls = env->FindClass("com/hook/fdmonitor/FdMonitorManager$JavaContext");
    if (temp_java_context_cls == NULL)  {
        LOGE("InitJniEnv kJavaBridgeClass NULL");
    }
    kJavaContextClass = reinterpret_cast<jclass>(env->NewGlobalRef(temp_java_context_cls));
    kFieldIDStack = env->GetFieldID(kJavaContextClass, "stack", "Ljava/lang/String;");
    kFieldIDThreadName = env->GetFieldID(kJavaContextClass, "threadName", "Ljava/lang/String;");
    if (kFieldIDStack == NULL || kFieldIDThreadName == NULL) {
        LOGE("InitJniEnv kJavaContextClass field NULL");
    }
    kMethodIDGetJavaContext = env->GetStaticMethodID(clazz, "getJavaContext", "()Lcom/hook/fdmonitor/FdMonitorManager$JavaContext;");
    if (kMethodIDGetJavaContext == NULL) {
        LOGE("InitJniEnv kMethodIDGetJavaContext NULL");
    }
    return JNI_VERSION_1_6; // 返回支持的 JNI 版本
}

