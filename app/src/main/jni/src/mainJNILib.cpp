#include "util.hpp"

extern "C" {
    #include <unistd.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <string.h>
    #include <stdio.h>
}

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/bitmap.h>
#include <utils/Mutex.h>
using namespace android;

#include <fpdfview.h>
#include <fpdf_doc.h>

//inclue the header file in library
#include <fpdf_text.h>


#include <string>
#include <vector>
#include <cstddef>

static Mutex sLibraryLock;

static int sLibraryReferenceCount = 0;


static void initLibraryIfNeed(){
    Mutex::Autolock lock(sLibraryLock);
    if(sLibraryReferenceCount == 0){
        LOGD("Init FPDF library");
        FPDF_InitLibrary();
    }
    sLibraryReferenceCount++;
}

static void destroyLibraryIfNeed(){
    Mutex::Autolock lock(sLibraryLock);

    sLibraryReferenceCount--;
    if(sLibraryReferenceCount == 0){
        LOGD("Destroy FPDF library");
        FPDF_DestroyLibrary();
    }
}

struct rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

class DocumentFile {
private:
    int fileFd;

public:
    FPDF_DOCUMENT pdfDocument = NULL;
    size_t fileSize;

    DocumentFile() { initLibraryIfNeed(); }
    ~DocumentFile();
};
DocumentFile::~DocumentFile(){
    if(pdfDocument != NULL){
        FPDF_CloseDocument(pdfDocument);
    }

    destroyLibraryIfNeed();
}

template <class string_type>
inline typename string_type::value_type* WriteInto(string_type* str, size_t length_with_null) {
    str->reserve(length_with_null);
    str->resize(length_with_null - 1);
    return &((*str)[0]);
}

inline long getFileSize(int fd){
    struct stat file_state;
    if(fstat(fd, &file_state) >= 0){
        return (long)(file_state.st_size);
    }else{
        LOGE("Error getting file size");
        return 0;
    }
}

static char* getErrorDescription(const long error) {
    char* description = NULL;
    switch(error) {
        case FPDF_ERR_SUCCESS:
            asprintf(&description, "No error.");
            break;
        case FPDF_ERR_FILE:
            asprintf(&description, "File not found or could not be opened.");
            break;
        case FPDF_ERR_FORMAT:
            asprintf(&description, "File not in PDF format or corrupted.");
            break;
        case FPDF_ERR_PASSWORD:
            asprintf(&description, "Incorrect password.");
            break;
        case FPDF_ERR_SECURITY:
            asprintf(&description, "Unsupported security scheme.");
            break;
        case FPDF_ERR_PAGE:
            asprintf(&description, "Page not found or content error.");
            break;
        default:
            asprintf(&description, "Unknown error.");
    }

    return description;
}

int jniThrowException(JNIEnv* env, const char* className, const char* message) {
    jclass exClass = env->FindClass(className);
    if (exClass == NULL) {
        LOGE("Unable to find exception class %s", className);
        return -1;
    }

    if(env->ThrowNew(exClass, message ) != JNI_OK) {
        LOGE("Failed throwing '%s' '%s'", className, message);
        return -1;
    }

    return 0;
}

int jniThrowExceptionFmt(JNIEnv* env, const char* className, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msgBuf[512];
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    return jniThrowException(env, className, msgBuf);
    va_end(args);
}

jobject NewLong(JNIEnv* env, jlong value) {
    jclass cls = env->FindClass("java/lang/Long");
    jmethodID methodID = env->GetMethodID(cls, "<init>", "(J)V");
    return env->NewObject(cls, methodID, value);
}

jobject NewInteger(JNIEnv* env, jint value) {
    jclass cls = env->FindClass("java/lang/Integer");
    jmethodID methodID = env->GetMethodID(cls, "<init>", "(I)V");
    return env->NewObject(cls, methodID, value);
}

uint16_t rgbTo565(rgb *color) {
    return ((color->red >> 3) << 11) | ((color->green >> 2) << 5) | (color->blue >> 3);
}

void rgbBitmapTo565(void *source, int sourceStride, void *dest, AndroidBitmapInfo *info) {
    rgb *srcLine;
    uint16_t *dstLine;
    int y, x;
    for (y = 0; y < info->height; y++) {
        srcLine = (rgb*) source;
        dstLine = (uint16_t*) dest;
        for (x = 0; x < info->width; x++) {
            dstLine[x] = rgbTo565(&srcLine[x]);
        }
        source = (char*) source + sourceStride;
        dest = (char*) dest + info->stride;
    }
}

extern "C" { //For JNI support

static int getBlock(void* param, unsigned long position, unsigned char* outBuffer,
                    unsigned long size) {
    const int fd = reinterpret_cast<intptr_t>(param);
    const int readCount = pread(fd, outBuffer, size, position);
    if (readCount < 0) {
        LOGE("Cannot read from file descriptor. Error:%d", errno);
        return 0;
    }
    return 1;
}



static jlong loadPageInternal(JNIEnv *env, DocumentFile *doc, int pageIndex){
    try{
        if(doc == NULL) throw "Get page document null";

        FPDF_DOCUMENT pdfDoc = doc->pdfDocument;
        if(pdfDoc != NULL){
            FPDF_PAGE page = FPDF_LoadPage(pdfDoc, pageIndex);
            if (page == NULL) {
                throw "Loaded page is null";
            }
            return reinterpret_cast<jlong>(page);
        }else{
            throw "Get page pdf document null";
        }

    }catch(const char *msg){
        LOGE("%s", msg);

        jniThrowException(env, "java/lang/IllegalStateException",
                          "cannot load page");

        return -1;
    }
}

static jlong loadTextPageInternal(JNIEnv *env, DocumentFile *doc, int textPageIndex){
    try{
        if(doc == NULL) throw "Get page document null";

        FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(loadPageInternal(env, doc, textPageIndex));
        if(page != NULL){
            FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
            if (textPage == NULL) {
                throw "Loaded text page is null";
            }
            return reinterpret_cast<jlong>(textPage);
        }else{
            throw "Load page null";
        }
    }catch(const char *msg){
        LOGE("%s", msg);

        jniThrowException(env, "java/lang/IllegalStateException",
                          "cannot load text page");

        return -1;
    }
}

static void closePageInternal(jlong pagePtr) { FPDF_ClosePage(reinterpret_cast<FPDF_PAGE>(pagePtr)); }




static void renderPageInternal( FPDF_PAGE page,
                                ANativeWindow_Buffer *windowBuffer,
                                int startX, int startY,
                                int canvasHorSize, int canvasVerSize,
                                int drawSizeHor, int drawSizeVer,
                                bool renderAnnot){

    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx( canvasHorSize, canvasVerSize,
                                                 FPDFBitmap_BGRA,
                                                 windowBuffer->bits, (int)(windowBuffer->stride) * 4);

    /*LOGD("Start X: %d", startX);
    LOGD("Start Y: %d", startY);
    LOGD("Canvas Hor: %d", canvasHorSize);
    LOGD("Canvas Ver: %d", canvasVerSize);
    LOGD("Draw Hor: %d", drawSizeHor);
    LOGD("Draw Ver: %d", drawSizeVer);*/

    if(drawSizeHor < canvasHorSize || drawSizeVer < canvasVerSize){
        FPDFBitmap_FillRect( pdfBitmap, 0, 0, canvasHorSize, canvasVerSize,
                             0x848484FF); //Gray
    }

    int baseHorSize = (canvasHorSize < drawSizeHor)? canvasHorSize : drawSizeHor;
    int baseVerSize = (canvasVerSize < drawSizeVer)? canvasVerSize : drawSizeVer;
    int baseX = (startX < 0)? 0 : startX;
    int baseY = (startY < 0)? 0 : startY;
    int flags = FPDF_REVERSE_BYTE_ORDER;

    if(renderAnnot) {
        flags |= FPDF_ANNOT;
    }

    FPDFBitmap_FillRect( pdfBitmap, baseX, baseY, baseHorSize, baseVerSize,
                         0xFFFFFFFF); //White

    FPDF_RenderPageBitmap( pdfBitmap, page,
                           startX, startY,
                           drawSizeHor, drawSizeVer,
                           0, flags );
}

}//extern C
extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndktesting_PdfiumCore_nativePageCoordsToDevice(JNIEnv *env, jobject thiz,
                                                             jlong page_ptr, jint start_x,
                                                             jint start_y, jint size_x, jint size_y,
                                                             jint rotate, jdouble page_x,
                                                             jdouble page_y) {
    // TODO: implement nativePageCoordsToDevice()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(page_ptr);
    int deviceX, deviceY;

    FPDF_PageToDevice(page, start_x, start_y, size_x, size_y, rotate, page_x, page_y, &deviceX, &deviceY);

    jclass clazz = env->FindClass("android/graphics/Point");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(II)V");
    return env->NewObject(clazz, constructorID, deviceX, deviceY);
}extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetLinkRect(JNIEnv *env, jobject thiz, jlong linkPtr) {
    // TODO: implement nativeGetLinkRect()
    FPDF_LINK link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FS_RECTF fsRectF;
    FPDF_BOOL result = FPDFLink_GetAnnotRect(link, &fsRectF);

    if (!result) {
        return NULL;
    }

    jclass clazz = env->FindClass("android/graphics/RectF");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(FFFF)V");
    return env->NewObject(clazz, constructorID, fsRectF.left, fsRectF.top, fsRectF.right, fsRectF.bottom);
}extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetLinkURI(JNIEnv *env, jobject thiz, jlong docPtr,
                                                     jlong linkPtr) {
    // TODO: implement nativeGetLinkURI()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_LINK link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FPDF_ACTION action = FPDFLink_GetAction(link);
    if (action == NULL) {
        return NULL;
    }
    size_t bufferLen = FPDFAction_GetURIPath(doc->pdfDocument, action, NULL, 0);
    if (bufferLen <= 0) {
        return env->NewStringUTF("");
    }
    std::string uri;
    FPDFAction_GetURIPath(doc->pdfDocument, action, WriteInto(&uri, bufferLen), bufferLen);
    return env->NewStringUTF(uri.c_str());
}extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetDestPageIndex(JNIEnv *env, jobject thiz, jlong docPtr,
                                                           jlong linkPtr) {
    // TODO: implement nativeGetDestPageIndex()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_LINK link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FPDF_DEST dest = FPDFLink_GetDest(doc->pdfDocument, link);
    if (dest == NULL) {
        return NULL;
    }
    unsigned long index = FPDFDest_GetPageIndex(doc->pdfDocument, dest);
    return NewInteger(env, (jint) index);
}extern "C"
JNIEXPORT jlongArray JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageLinks(JNIEnv *env, jobject thiz, jlong pagePtr) {
    // TODO: implement nativeGetPageLinks()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    int pos = 0;
    std::vector<jlong> links;
    FPDF_LINK link;
    while (FPDFLink_Enumerate(page, &pos, &link)) {
        links.push_back(reinterpret_cast<jlong>(link));
    }

    jlongArray result = env->NewLongArray(links.size());
    env->SetLongArrayRegion(result, 0, links.size(), &links[0]);
    return result;
}extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageSizeByIndex(JNIEnv *env, jobject thiz,
                                                             jlong docPtr, jint pageIndex,
                                                             jint dpi) {
    // TODO: implement nativeGetPageSizeByIndex()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    if(doc == NULL) {
        LOGE("Document is null");

        jniThrowException(env, "java/lang/IllegalStateException",
                          "Document is null");
        return NULL;
    }

    double width, height;
    int result = FPDF_GetPageSizeByIndex(doc->pdfDocument, pageIndex, &width, &height);

    if (result == 0) {
        width = 0;
        height = 0;
    }

    jint widthInt = (jint) (width * dpi / 72);
    jint heightInt = (jint) (height * dpi / 72);

    jclass clazz = env->FindClass("com/example/ndktesting/util/Size");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(II)V");
    return env->NewObject(clazz, constructorID, widthInt, heightInt);
}extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetBookmarkDestIndex(JNIEnv *env, jobject thiz,
                                                               jlong docPtr, jlong bookmarkPtr) {
    // TODO: implement nativeGetBookmarkDestIndex()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_BOOKMARK bookmark = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);

    FPDF_DEST dest = FPDFBookmark_GetDest(doc->pdfDocument, bookmark);
    if (dest == NULL) {
        return -1;
    }
    return (jlong) FPDFDest_GetPageIndex(doc->pdfDocument, dest);
}extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetBookmarkTitle(JNIEnv *env, jobject thiz,
                                                           jlong bookmarkPtr) {
    // TODO: implement nativeGetBookmarkTitle()
    FPDF_BOOKMARK bookmark = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    size_t bufferLen = FPDFBookmark_GetTitle(bookmark, NULL, 0);
    if (bufferLen <= 2) {
        return env->NewStringUTF("");
    }
    std::wstring title;
    FPDFBookmark_GetTitle(bookmark, WriteInto(&title, bufferLen + 1), bufferLen);
    return env->NewString((jchar*) title.c_str(), bufferLen / 2 - 1);
}extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetSiblingBookmark(JNIEnv *env, jobject thiz,
                                                             jlong docPtr, jlong bookmarkPtr) {
    // TODO: implement nativeGetSiblingBookmark()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_BOOKMARK parent = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetNextSibling(doc->pdfDocument, parent);
    if (bookmark == NULL) {
        return NULL;
    }
    return NewLong(env, reinterpret_cast<jlong>(bookmark));
}extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetFirstChildBookmark(JNIEnv *env, jobject thiz,
                                                                jlong docPtr,
                                                                jobject bookmarkPtr) {
    // TODO: implement nativeGetFirstChildBookmark()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_BOOKMARK parent;
    if(bookmarkPtr == NULL) {
        parent = NULL;
    } else {
        jclass longClass = env->GetObjectClass(bookmarkPtr);
        jmethodID longValueMethod = env->GetMethodID(longClass, "longValue", "()J");

        jlong ptr = env->CallLongMethod(bookmarkPtr, longValueMethod);
        parent = reinterpret_cast<FPDF_BOOKMARK>(ptr);
    }
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetFirstChild(doc->pdfDocument, parent);
    if (bookmark == NULL) {
        return NULL;
    }
    return NewLong(env, reinterpret_cast<jlong>(bookmark));
}extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetDocumentMetaText(JNIEnv *env, jobject thiz,
                                                              jlong docPtr, jstring tag) {
    // TODO: implement nativeGetDocumentMetaText()

    const char *ctag = env->GetStringUTFChars(tag, NULL);
    if (ctag == NULL) {
        return env->NewStringUTF("");
    }
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);

    size_t bufferLen = FPDF_GetMetaText(doc->pdfDocument, ctag, NULL, 0);
    if (bufferLen <= 2) {
        return env->NewStringUTF("");
    }
    std::wstring text;
    FPDF_GetMetaText(doc->pdfDocument, ctag, WriteInto(&text, bufferLen + 1), bufferLen);
    env->ReleaseStringUTFChars(tag, ctag);
    return env->NewString((jchar*) text.c_str(), bufferLen / 2 - 1);
}extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeRenderPageBitmap(JNIEnv *env, jobject thiz,
                                                           jlong page_ptr, jobject bitmap, jint dpi,
                                                           jint start_x, jint start_y,
                                                           jint drawSizeHor, jint drawSizeVer,
                                                           jboolean render_annot) {
    // TODO: implement nativeRenderPageBitmap()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(page_ptr);

    if(page == NULL || bitmap == NULL){
        LOGE("Render page pointers invalid");
        return;
    }

    AndroidBitmapInfo info;
    int ret;
    if((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("Fetching bitmap info failed: %s", strerror(ret * -1));
        return;
    }

    int canvasHorSize = info.width;
    int canvasVerSize = info.height;

    if(info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 && info.format != ANDROID_BITMAP_FORMAT_RGB_565){
        LOGE("Bitmap format must be RGBA_8888 or RGB_565");
        return;
    }

    void *addr;
    if( (ret = AndroidBitmap_lockPixels(env, bitmap, &addr)) != 0 ){
        LOGE("Locking bitmap failed: %s", strerror(ret * -1));
        return;
    }

    void *tmp;
    int format;
    int sourceStride;
    if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
        tmp = malloc(canvasVerSize * canvasHorSize * sizeof(rgb));
        sourceStride = canvasHorSize * sizeof(rgb);
        format = FPDFBitmap_BGR;
    } else {
        tmp = addr;
        sourceStride = info.stride;
        format = FPDFBitmap_BGRA;
    }

    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx( canvasHorSize, canvasVerSize,
                                                 format, tmp, sourceStride);

/*LOGD("Start X: %d", startX);
LOGD("Start Y: %d", startY);
LOGD("Canvas Hor: %d", canvasHorSize);
LOGD("Canvas Ver: %d", canvasVerSize);
LOGD("Draw Hor: %d", drawSizeHor);
LOGD("Draw Ver: %d", drawSizeVer);*/

    if(drawSizeHor < canvasHorSize || drawSizeVer < canvasVerSize){
        FPDFBitmap_FillRect( pdfBitmap, 0, 0, canvasHorSize, canvasVerSize,
                             0x848484FF); //Gray
    }

    int baseHorSize = (canvasHorSize < drawSizeHor)? canvasHorSize : (int)drawSizeHor;
    int baseVerSize = (canvasVerSize < drawSizeVer)? canvasVerSize : (int)drawSizeVer;
    int baseX = (start_x < 0)? 0 : (int)start_x;
    int baseY = (start_y < 0)? 0 : (int)start_y;
    int flags = FPDF_REVERSE_BYTE_ORDER;

    if(render_annot) {
        flags |= FPDF_ANNOT;
    }

    FPDFBitmap_FillRect( pdfBitmap, baseX, baseY, baseHorSize, baseVerSize,
                         0xFFFFFFFF); //White

    FPDF_RenderPageBitmap( pdfBitmap, page,
                           start_x, start_y,
                           (int)drawSizeHor, (int)drawSizeVer,
                           0, flags );

    if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
        rgbBitmapTo565(tmp, sourceStride, addr, &info);
        free(tmp);
    }

    AndroidBitmap_unlockPixels(env, bitmap);
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeRenderPage(JNIEnv *env, jobject thiz, jlong page_ptr,
                                                     jobject surface, jint dpi, jint start_x,
                                                     jint start_y, jint draw_size_hor,
                                                     jint draw_size_ver, jboolean render_annot) {
    // TODO: implement nativeRenderPage()
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    if(nativeWindow == NULL){
        LOGE("native window pointer null");
        return;
    }
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(page_ptr);

    if(page == NULL || nativeWindow == NULL){
        LOGE("Render page pointers invalid");
        return;
    }

    if(ANativeWindow_getFormat(nativeWindow) != WINDOW_FORMAT_RGBA_8888){
        LOGD("Set format to RGBA_8888");
        ANativeWindow_setBuffersGeometry( nativeWindow,
                                          ANativeWindow_getWidth(nativeWindow),
                                          ANativeWindow_getHeight(nativeWindow),
                                          WINDOW_FORMAT_RGBA_8888 );
    }

    ANativeWindow_Buffer buffer;
    int ret;
    if( (ret = ANativeWindow_lock(nativeWindow, &buffer, NULL)) != 0 ){
        LOGE("Locking native window failed: %s", strerror(ret * -1));
        return;
    }

    renderPageInternal(page, &buffer,
                       (int)start_x, (int)start_y,
                       buffer.width, buffer.height,
                       (int)draw_size_hor, (int)draw_size_ver,
                       (bool)render_annot);

    ANativeWindow_unlockAndPost(nativeWindow);
    ANativeWindow_release(nativeWindow);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageHeightPoint(JNIEnv *env, jobject thiz,
                                                             jlong page_ptr) {
    // TODO: implement nativeGetPageHeightPoint()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(page_ptr);
    return (jint)FPDF_GetPageHeight(page);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageWidthPoint(JNIEnv *env, jobject thiz,
                                                            jlong pagePtr) {
    // TODO: implement nativeGetPageWidthPoint()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)FPDF_GetPageWidth(page);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageHeightPixel(JNIEnv *env, jobject thiz,
                                                             jlong pagePtr, jint dpi) {
    // TODO: implement nativeGetPageHeightPixel()

    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)(FPDF_GetPageHeight(page) * dpi / 72);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageWidthPixel(JNIEnv *env, jobject thiz,
                                                            jlong pagePtr, jint dpi) {
    // TODO: implement nativeGetPageWidthPixel()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)(FPDF_GetPageWidth(page) * dpi / 72);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeClosePages(JNIEnv *env, jobject thiz,
                                                     jlongArray pages_ptr) {
    // TODO: implement nativeClosePages()
    int length = (int)(env -> GetArrayLength(pages_ptr));
    jlong *pages = env -> GetLongArrayElements(pages_ptr, NULL);

    int i;
    for(i = 0; i < length; i++){ closePageInternal(pages[i]); }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeClosePage(JNIEnv *env, jobject thiz, jlong page_ptr) {
    // TODO: implement nativeClosePage()
    closePageInternal(page_ptr);
}

extern "C"
JNIEXPORT jlongArray JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeLoadPages(JNIEnv *env, jobject thiz, jlong doc_ptr,
                                                    jint from_index, jint to_index) {
    // TODO: implement nativeLoadPages()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(doc_ptr);

    if(to_index < from_index) return NULL;
    jlong pages[ to_index - from_index + 1 ];

    int i;
    for(i = 0; i <= (to_index - from_index); i++){
        pages[i] = loadPageInternal(env, doc, (int)(i + from_index));
    }

    jlongArray javaPages = env -> NewLongArray( (jsize)(to_index - from_index + 1) );
    env -> SetLongArrayRegion(javaPages, 0, (jsize)(to_index - from_index + 1), (const jlong*)pages);

    return javaPages;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeLoadPage(JNIEnv *env, jobject thiz, jlong doc_ptr,
                                                   jint page_index) {
    // TODO: implement nativeLoadPage()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(doc_ptr);
    return loadPageInternal(env, doc, (int)page_index);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetPageCount(JNIEnv *env, jobject thiz, jlong doc_ptr) {
    // TODO: implement nativeGetPageCount()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(doc_ptr);
    return (jint)FPDF_GetPageCount(doc->pdfDocument);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ndktesting_MainActivity_nativetest(JNIEnv *env, jobject /*thiz*/) {
    // TODO: implement nativetest()
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeCloseDocument(JNIEnv *env, jobject thiz, jlong doc_ptr) {
    // TODO: implement nativeCloseDocument()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(doc_ptr);

    delete doc;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeOpenMemDocument(JNIEnv *env, jobject thiz,
                                                          jbyteArray data, jstring password) {
    // TODO: implement nativeOpenMemDocument()
    DocumentFile *docFile = new DocumentFile();

    const char *cpassword = NULL;
    if(password != NULL) {
        cpassword = env->GetStringUTFChars(password, NULL);
    }

    jbyte *cData = env->GetByteArrayElements(data, NULL);
    int size = (int) env->GetArrayLength(data);
    jbyte *cDataCopy = new jbyte[size];
    memcpy(cDataCopy, cData, size);
    FPDF_DOCUMENT document = FPDF_LoadMemDocument( reinterpret_cast<const void*>(cDataCopy),
                                                   size, cpassword);
    env->ReleaseByteArrayElements(data, cData, JNI_ABORT);

    if(cpassword != NULL) {
        env->ReleaseStringUTFChars(password, cpassword);
    }

    if (!document) {
        delete docFile;

        const long errorNum = FPDF_GetLastError();
        if(errorNum == FPDF_ERR_PASSWORD) {
            jniThrowException(env, "com/shockwave/pdfium/PdfPasswordException",
                              "Password required or incorrect password.");
        } else {
            char* error = getErrorDescription(errorNum);
            jniThrowExceptionFmt(env, "java/io/IOException",
                                 "cannot create document: %s", error);

            free(error);
        }

        return -1;
    }

    docFile->pdfDocument = document;

    return reinterpret_cast<jlong>(docFile);

}


extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeOpenDocument(JNIEnv *env, jobject thiz, jint fd,
                                                       jstring password) {
    // TODO: implement nativeOpenDocument()
    size_t fileLength = (size_t) getFileSize(fd);
    if (fileLength <= 0) {
        jniThrowException(env, "java/io/IOException",
                          "File is empty");
        return -1;
    }

    DocumentFile *docFile = new DocumentFile();

    FPDF_FILEACCESS loader;
    loader.m_FileLen = fileLength;
    loader.m_Param = reinterpret_cast<void *>(intptr_t(fd));
    loader.m_GetBlock = &getBlock;

    const char *cpassword = NULL;
    if (password != NULL) {
        cpassword = env->GetStringUTFChars(password, NULL);
    }

    FPDF_DOCUMENT document = FPDF_LoadCustomDocument(&loader, cpassword);

    if (cpassword != NULL) {
        env->ReleaseStringUTFChars(password, cpassword);
    }

    if (!document) {
        delete docFile;

        const long errorNum = FPDF_GetLastError();
        if (errorNum == FPDF_ERR_PASSWORD) {
            jniThrowException(env, "com/shockwave/pdfium/PdfPasswordException",
                              "Password required or incorrect password.");
        } else {
            char *error = getErrorDescription(errorNum);
            jniThrowExceptionFmt(env, "java/io/IOException",
                                 "cannot create document: %s", error);

            free(error);
        }

        return -1;
    }

    docFile->pdfDocument = document;

    return reinterpret_cast<jlong>(docFile);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_MainActivity_nativeadd(JNIEnv *env, jobject thiz, jint value) {
    // TODO: implement nativeadd()

    int sum  = value + 10;

    return sum;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeadd(JNIEnv *env, jobject thiz, jint value) {
    // TODO: implement nativeadd()
    int sum  = value + 10;

    return sum;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextLoadPage(JNIEnv *env, jobject thiz,
                                                          jlong page_ptr) {
    // TODO: implement nativeTextLoadPage()
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(page_ptr);

    return reinterpret_cast<jlong>(FPDFText_LoadPage(page));
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetTotalCharactersInPage(JNIEnv *env, jobject thiz,
                                                                      jlong page_ptr) {
    // TODO: implement nativeGetTotalCharactersInPage()

    FPDF_TEXTPAGE page = reinterpret_cast<FPDF_TEXTPAGE>(page_ptr);


    return (jint)FPDFText_CountChars(page);


}extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeCloseTextpage(JNIEnv *env, jobject thiz,
                                                           jlong page_ptr) {
    // TODO: implement nativeCloseTextpage()
    FPDF_TEXTPAGE page = reinterpret_cast<FPDF_TEXTPAGE>(page_ptr);

    FPDFText_ClosePage(page);

}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextSearchHandler(JNIEnv *env, jobject thiz,
                                                               jlong page_ptr, jint start_index,
                                                               jstring word) {
    // TODO: implement nativeTextSearchHandler()
    FPDF_TEXTPAGE page = reinterpret_cast<FPDF_TEXTPAGE>(page_ptr);

    const char *ctag = env->GetStringUTFChars(word, NULL);

    int length = env->GetStringLength(word);
    const FPDF_WCHAR* wcFind = env->GetStringChars(word, 0);




    return reinterpret_cast<jlong>(FPDFText_FindStart(page, (FPDF_WCHAR*)wcFind,
                                                      FPDF_MATCHCASE, start_index));
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeIfMatchFound(JNIEnv *env, jobject thiz,
                                                          jlong handler) {

    // TODO: implement nativeIfMatchFound()

   // return (jint*)searchHandle;
    FPDF_SCHHANDLE pSearchHandle = reinterpret_cast<FPDF_SCHHANDLE>(handler);
    FPDF_BOOL isMatch = FPDFText_FindNext(pSearchHandle);
    LOGD("FPDFText_FindNext Match is %x",isMatch);
    return isMatch;


    //return FPDFText_FindNext(reinterpret_cast<FPDF_SCHHANDLE>(handler));
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetText(JNIEnv *env, jobject thiz, jlong pageptr,
                                                     jint start, jint count) {
    // TODO: implement nativeGetText()
    FPDF_DWORD bufflen = 0;

    FPDF_TEXTPAGE pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(pageptr);

    FPDF_WCHAR* pBuff = new FPDF_WCHAR[count+1];


    int ret = FPDFText_GetText(pTextPage, start, count, pBuff);

    if(ret == 0){
        LOGE("FPDFTextGetText: FPDFTextGetText did not return success");
    }
    //LOGE("FPDFTextGetText: %x" , ret);
    jstring Text = env->NewString(pBuff, ret-1);
    //delete pBuff;

    return Text;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetSearchCount(JNIEnv *env, jobject thiz,
                                                            jlong handler) {
    // TODO: implement nativeGetSearchCount()
    FPDF_SCHHANDLE pSearchHandle = reinterpret_cast<FPDF_SCHHANDLE>(handler);
    jint result = FPDFText_GetSchCount(pSearchHandle);

    LOGE("FPDFTextcount: %x" , FPDFText_GetSchCount(pSearchHandle));
    return result;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_ndktesting_PdfiumCore_nativePreviousMatch(JNIEnv *env, jobject thiz,
                                                           jlong handler) {
    // TODO: implement nativePreviousMatch()
    FPDF_SCHHANDLE pSearchHandle = reinterpret_cast<FPDF_SCHHANDLE>(handler);

    return FPDFText_FindPrev(pSearchHandle);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeGetSearchIndex(JNIEnv *env, jobject thiz,
                                                            jlong handler) {
    // TODO: implement nativeGetSearchIndex()
    FPDF_SCHHANDLE pSearchHandle = reinterpret_cast<FPDF_SCHHANDLE>(handler);

    return FPDFText_GetSchResultIndex(pSearchHandle);
}

extern "C"
JNIEXPORT jdoubleArray JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextGetCharBox(JNIEnv *env, jobject thiz,
                                                            jlong text_page_ptr, jint index) {
    // TODO: implement nativeTextGetCharBox()

    FPDF_TEXTPAGE *textPage = reinterpret_cast<FPDF_TEXTPAGE*>(text_page_ptr);
    jdoubleArray result = env->NewDoubleArray(4);
    if (result == NULL) {
        return NULL;
    }
    double fill[4];
    FPDFText_GetCharBox(textPage, (int)index, &fill[0], &fill[1], &fill[2], &fill[3]);
    env->SetDoubleArrayRegion(result, 0, 4, (jdouble*)fill);
    return result;
}extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextGetCharIndexAtPos(JNIEnv *env, jobject thiz,
                                                                   jlong text_page_ptr, jdouble x,
                                                                   jdouble y, jdouble x_tolerance,
                                                                   jdouble y_tolerance) {
    // TODO: implement nativeTextGetCharIndexAtPos()

    FPDF_TEXTPAGE *textPage = reinterpret_cast<FPDF_TEXTPAGE*>(text_page_ptr);
    return (jint)FPDFText_GetCharIndexAtPos(textPage, (double)x, (double)y, (double)x_tolerance, (double)y_tolerance);
}extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextCountRects(JNIEnv *env, jobject thiz,
                                                            jlong text_page_ptr, jint start_index,
                                                            jint count) {
    // TODO: implement nativeTextCountRects()
    FPDF_TEXTPAGE *textPage = reinterpret_cast<FPDF_TEXTPAGE*>(text_page_ptr);
    return (jint)FPDFText_CountRects(textPage, (int)start_index, (int) count);
}extern "C"
JNIEXPORT jdoubleArray JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextGetRect(JNIEnv *env, jobject thiz,
                                                         jlong text_page_ptr, jint rect_index) {
    // TODO: implement nativeTextGetRect()
    FPDF_TEXTPAGE *textPage = reinterpret_cast<FPDF_TEXTPAGE*>(text_page_ptr);
    jdoubleArray result = env->NewDoubleArray(4);
    if (result == NULL) {
        return NULL;
    }
    double fill[4];
    FPDFText_GetRect(textPage, (int)rect_index, &fill[0], &fill[1], &fill[2], &fill[3]);
    env->SetDoubleArrayRegion(result, 0, 4, (jdouble*)fill);
    return result;
}extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextGetBoundedText(JNIEnv *env, jobject thiz,
                                                                jlong text_page_ptr, jdouble left,
                                                                jdouble top, jdouble right,
                                                                jdouble bottom, jshortArray arr) {
    // TODO: implement nativeTextGetBoundedText()
    FPDF_TEXTPAGE *textPage = reinterpret_cast<FPDF_TEXTPAGE*>(text_page_ptr);
    jboolean isCopy = 0;
    unsigned short *buffer = NULL;
    int bufLen = 0;
    if (arr != NULL) {
        buffer = (unsigned short *)env->GetShortArrayElements(arr, &isCopy);
        bufLen = env->GetArrayLength(arr);
    }
    jint output = (jint)FPDFText_GetBoundedText(textPage, (double)left, (double)top, (double)right, (double)bottom, buffer, bufLen);
    if (isCopy) {
        env->SetShortArrayRegion(arr, 0, output, (jshort*)buffer);
        env->ReleaseShortArrayElements(arr, (jshort*)buffer, JNI_ABORT);
    }
    return output;
}extern "C"
JNIEXPORT jlongArray JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeLoadTextPages(JNIEnv *env, jobject thiz, jlong doc_ptr,
                                                           jint fromIndex, jint toIndex) {
    // TODO: implement nativeLoadTextPages()
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(doc_ptr);

    if(toIndex < fromIndex) return NULL;
    jlong pages[ toIndex - fromIndex + 1 ];

    int i;
    for(i = 0; i <= (toIndex - fromIndex); i++){
        pages[i] = loadTextPageInternal(env, doc, (int)(i + fromIndex));
    }

    jlongArray javaPages = env -> NewLongArray( (jsize)(toIndex - fromIndex + 1) );
    env -> SetLongArrayRegion(javaPages, 0, (jsize)(toIndex - fromIndex + 1), (const jlong*)pages);

    return javaPages;
}extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndktesting_PdfiumCore_nativeTextGetUnicode(JNIEnv *env, jobject thiz,
                                                            jlong text_page_ptr, jint index) {
    // TODO: implement nativeTextGetUnicode()
    FPDF_TEXTPAGE *textPage = reinterpret_cast<FPDF_TEXTPAGE*>(text_page_ptr);
    return (jint)FPDFText_GetUnicode(textPage, (int)index);
}