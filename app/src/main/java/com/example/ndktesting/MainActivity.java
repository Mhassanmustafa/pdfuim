package com.example.ndktesting;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

public class MainActivity extends AppCompatActivity {
    static {
        try {
            System.loadLibrary("jniPdfium");
            System.loadLibrary("c++_shared");
            System.loadLibrary("modpng");
            System.loadLibrary("modft2");
            System.loadLibrary("modpdfium");

        } catch (UnsatisfiedLinkError e) {

            e.printStackTrace();
            //Log.e(TAG, "Native libraries failed to load - " + e);
        }
    }


//    public String test (String value){
//
//        String val =  nativetest(value);
//        return val;
//    }
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);



        PdfiumCore pdfiumCore = new PdfiumCore();
      //  String value = test("my name is hassan");
        TextView tv = findViewById(R.id.sample_text);
        tv.setText("hello now import jni in it" + nativetest() + pdfiumCore.testnat(10));
    }

    private native String nativetest();

//

}
