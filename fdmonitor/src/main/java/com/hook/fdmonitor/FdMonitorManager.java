package com.hook.fdmonitor;

import android.content.Context;
import android.util.Log;
import com.bytedance.android.bytehook.ByteHook;
import com.tencent.mmkv.MMKV;

public class FdMonitorManager {
    private static String TAG = "FdMonitorManager";
    public static void init(Context context, boolean enablePrintLog){
        ByteHook.init();
        String rootDir = MMKV.initialize(context);
        Log.i(TAG, "mmkv root: " + rootDir);
        System.loadLibrary("fdmonitor");
        initMonitor(enablePrintLog);
    }
    public static native void initMonitor(boolean enablePrintLog);

    public static void saveBackTrace(int fd, String log){
        Log.i(TAG, "saveBackTrace--->" + fd);
        MMKV.defaultMMKV().putString(String.valueOf(fd), log);
        FdUtils.INSTANCE.printFd();
    }

    public static void removeBackTrace(int fd){
        Log.i(TAG, "removeBackTrace--->" + fd);
        MMKV.defaultMMKV().remove(String.valueOf(fd));
        FdUtils.INSTANCE.printFd();
    }
}
