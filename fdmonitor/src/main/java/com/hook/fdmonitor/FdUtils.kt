package com.hook.fdmonitor

import android.os.Process
import android.system.Os
import android.util.Log
import java.io.File

object FdUtils {
    const val TAG = "FdUtils"
    fun printFd() {
        val fdFile = File("/proc/" + Process.myPid() + "/fd/")
        // 列出目录下所有的fd情况
        val files = fdFile.listFiles()
        // 进程中的fd数量
        val size = files?.size
        // 我们在这里，能够拿到关键的信息，就是当前fd的数量
        files?.forEachIndexed { index, file ->
            try {
                // Os.readlink()是一个在Android系统中用于读取符号链接目标路径的函数
                val linkPath = Os.readlink(file.absolutePath);
                //获取到fd所指向的信息之后，其实返回的是一个String类型的文件路径，我们可以上报到自己的监控系统中
                Log.i(TAG, "$file : $linkPath")
            } catch (e: Exception) {
                //ignore
            }
        }

    }
    fun getFdNum(): Int {
        val fdFile = File("/proc/" + Process.myPid() + "/fd/")
        // 列出目录下所有的fd情况
        val files = fdFile.listFiles()
        // 进程中的fd数量
        val size = files?.size
        return size ?: 0
    }
}