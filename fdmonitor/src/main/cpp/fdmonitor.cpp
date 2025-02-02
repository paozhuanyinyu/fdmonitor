#include <jni.h>
#include <string>
#include "bytehook.h"
#include "xunwind.h"
#include <unistd.h>
#include <android/log.h>
#include <sstream>

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
JNIEnv* GetJNIEnv() {
    JNIEnv* env = nullptr;
    if (gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        gJavaVM->AttachCurrentThread(&env, nullptr);
    }
    return env;
}
jclass clazz;

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

void split_lines(const std::string& str) {
    std::stringstream ss(str);
    std::string line;
    while (std::getline(ss, line)) {
        LOGI("%s", line.c_str());
    }
}
void print_open_strace(int fd) {
    pid_t pid = getpid();
    pid_t tid = gettid();
    void *context = nullptr;
//    xunwind_cfi_log(pid, tid, context, SAMPLE_LOG_TAG,
//                    SAMPLE_LOG_PRIORITY, NULL);
    char * log = xunwind_cfi_get(pid, tid, context, NULL);
    int length = strlen(log);
    LOGI("log length: %d", length);
    saveBackTrace(fd, log);
    split_lines(log);
    free(log);
}

void print_close_strace(int fd) {
    pid_t pid = getpid();
    pid_t tid = gettid();
    void *context = nullptr;
//    xunwind_cfi_log(pid, tid, context, SAMPLE_LOG_TAG,
//                    SAMPLE_LOG_PRIORITY, NULL);
    char * log = xunwind_cfi_get(pid, tid, context, NULL);
    int length = strlen(log);
    LOGI("log length: %d", length);
    removeBackTrace(fd);
    split_lines(log);
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
        jclass jclazz) {
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

