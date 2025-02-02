package com.example.bhookdemo;

import android.content.Context;
import android.os.Environment;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class ImageUtils {
    public static boolean saveImageToStorage(Context context, String assetName, String fileName) {
        try {
            // 获取 assets 目录下的图片
            InputStream inputStream = context.getAssets().open(assetName);

            // 创建保存目录
            File dir = new File(context.getFilesDir().getAbsolutePath() + "/Bhook");
            if (!dir.exists()) {
                dir.mkdirs();
            }

            // 创建目标文件
            File outFile = new File(dir, fileName);

            // 写入文件
            OutputStream outputStream = new FileOutputStream(outFile);
            byte[] buffer = new byte[1024];
            int length;
            while ((length = inputStream.read(buffer)) > 0) {
                outputStream.write(buffer, 0, length);
            }

            // 关闭流
            outputStream.flush();
            outputStream.close();
            inputStream.close();

            return true;
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
    }
}
