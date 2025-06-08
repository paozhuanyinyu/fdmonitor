package com.example.bhookdemo

import android.content.Context
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.example.bhookdemo.ui.theme.BhookDemoTheme


class MainActivity : ComponentActivity() {
    companion object {
         val PERMISSION_REQUEST_CODE = 1001
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            BhookDemoTheme {
                // A surface container using the 'background' color from the theme
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    Greeting("Android")
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Button(
                            onClick = {
                                // 保存图片
                                val success = ImageUtils.saveImageToStorage(
                                    this@MainActivity,
                                    "image/my_image.png",  // assets 中的图片路径
                                    "saved_image.png" // 保存后的文件名
                                )

                                if (success) {
                                    Toast.makeText(this@MainActivity, "图片保存成功", Toast.LENGTH_SHORT)
                                        .show()
                                } else {
                                    Toast.makeText(this@MainActivity, "图片保存失败", Toast.LENGTH_SHORT)
                                        .show()
                                }
                            },
                            modifier = Modifier.wrapContentSize()
                        ) {
                            Text(text = "保存图片")
                        }
                        Button(
                            onClick = {
                                // 删除图片
                                val success = ImageUtils.deleteImage(
                                    this@MainActivity,
                                    "saved_image.png" // 保存后的文件名
                                )
                                if (success) {
                                    Toast.makeText(this@MainActivity, "图片删除成功", Toast.LENGTH_SHORT)
                                        .show()
                                } else {
                                    Toast.makeText(this@MainActivity, "图片删除失败", Toast.LENGTH_SHORT)
                                        .show()
                                }
                            },
                            modifier = Modifier.wrapContentSize()
                        ) {
                            Text(text = "删除图片")
                        }
                    }

                }
            }
        }
    }
}

@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text(
        text = "Hello $name!",
        modifier = modifier
    )
}
@Composable
fun SavePhoto(context: Context, name: String, action: () -> Unit) {
    Button(
        onClick = action,
        modifier = Modifier.height(50.dp)
    ) {
        Text(text = name)
    }
}

@Preview(showBackground = true)
@Composable
fun GreetingPreview() {
    BhookDemoTheme {
        Greeting("Android")
    }
}