package com.example.bhookdemo

import android.app.Application
import com.hook.fdmonitor.FdMonitorManager

class MyApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        FdMonitorManager.init(this, true);
    }
}