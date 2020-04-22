package com.example.ndktesting;

import androidx.annotation.RequiresApi;
import androidx.appcompat.app.AppCompatActivity;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Paths;

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
    @RequiresApi(api = Build.VERSION_CODES.O)
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);



        PdfiumCore pdfiumCore = new PdfiumCore();
      //  String value = test("my name is hassan");
        System.out.println("hello this is flag");
        //File pdfFile = new File("/home/hassan/Desktop/andriod/ndktesting/artificialintelligenceamodernapproachbyrussellnorvig3rd-141011032110-conversion-gate02.pdf");
//        Uri path = Uri.fromFile(pdfFile);
//        Intent intent = new Intent(Intent.ACTION_VIEW);
//        intent.setDataAndType(path, "application/pdf");
//        intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);

//        if(pdfFile.exists()){
//            System.out.println("hurry");
//        }else {
//            System.out.println(intent.hasFileDescriptors());
//        }
        PdfDocument pdfDocument;
        try {
            InputStream is = getAssets().open("artificialintelligenceamodernapproachbyrussellnorvig3rd-141011032110-conversion-gate02.pdf");
            System.out.println(is.available());
            byte[] data = new byte[is.available()];
            is.read(data);
            is.close();

            pdfDocument =  pdfiumCore.newDocument(data,null);


            System.out.println("this is my mark   " + pdfiumCore.getPageCharcters(pdfiumCore.getPdfTextPageLoad(pdfDocument,4)));
            System.out.println(new String("hassan".getBytes("UTF-8"),"UTF-16LE"));
            System.out.println("checking match  " + pdfiumCore.SearchWord("book",pdfiumCore.getPdfTextPageLoad(pdfDocument,8)));

            System.out.println("new chechking" + pdfiumCore.getText(pdfiumCore.getPdfTextPageLoad(pdfDocument,4),0,436));
        } catch (IOException e) {
            e.printStackTrace();
        }


        //PdfDocument pdfDocument ;
        //File file = new File(String.valueOf(Paths.get("//home//hassan/Desktop//andriod//ndktesting//artificialintelligenceamodernapproachbyrussellnorvig3rd-141011032110-conversion-gate02.pdf")));
//        try {
//            ParcelFileDescriptor p = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
//
//
//            System.out.println("ghe " + pdfiumCore.getPageCount(pdfDocument));
//        }catch (Exception e){
//            e.printStackTrace();
//        }
        TextView tv = findViewById(R.id.sample_text);

        tv.setText("hello now import jni in it" + nativetest() + pdfiumCore.testnat(10));
    }

    private native String nativetest();

//

}
