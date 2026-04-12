type VideoCapture*   {.importcpp: "cv::VideoCapture", header: "<opencv2/videoio.hpp>".} = object
type Mat*            {.importcpp: "cv::Mat",           header: "<opencv2/core.hpp>".}    = object
type Size*           {.importcpp: "cv::Size",          header: "<opencv2/core.hpp>".}    = object
type Scalar*         {.importcpp: "cv::Scalar",        header: "<opencv2/core.hpp>".}    = object
type Point*          {.importcpp: "cv::Point",         header: "<opencv2/core.hpp>".}    = object
type Rect*           {.importcpp: "cv::Rect",          header: "<opencv2/core.hpp>".}    = object
type FaceDetectorYN* {.importcpp: "cv::Ptr<cv::FaceDetectorYN>", header: "<opencv2/objdetect.hpp>".} = object
type FaceParams* = object
  version*: uint32
  angleX*, angleY*, angleZ*, mouthOpen*: float32
  mouthForm*: float32
  eyeBallX*, eyeBallY*: float32
  eyeOpenL*, eyeOpenR*: float32
  browL*, browR*: float32

proc namedWindow*(name: cstring, flags: cint = 0): void {.importcpp: "cv::namedWindow(@)", header: "<opencv2/highgui.hpp>".}
proc waitKey*(delay: cint = 0): cint {.importcpp: "cv::waitKey(@)", header: "<opencv2/highgui.hpp>".}
proc destroyAllWindows*(): void {.importcpp: "cv::destroyAllWindows()", header: "<opencv2/highgui.hpp>".}
proc imshow*(winname: cstring, mat: Mat): void {.importcpp: "cv::imshow(@)", header: "<opencv2/highgui.hpp>".}

proc isOpened*(self: VideoCapture): bool {.importcpp: "#.isOpened()", header: "<opencv2/videoio.hpp>".}
proc release*(self: VideoCapture): void {.importcpp: "#.release()", header: "<opencv2/videoio.hpp>".}
proc read*(self: VideoCapture, frame: var Mat): bool {.importcpp: "#.read(@)", header: "<opencv2/videoio.hpp>".}
proc get*(self: VideoCapture, propId: cint): float64 {.importcpp: "#.get(@)", header: "<opencv2/videoio.hpp>".}
proc set*(self: VideoCapture, propId: cint, value: float64): bool {.importcpp: "#.set(@)", header: "<opencv2/videoio.hpp>".}

proc empty*(self: Mat): bool {.importcpp: "#.empty()", header: "<opencv2/core.hpp>".}
proc blobFromImage*(image: Mat, scalefactor: float64 = 1.0, size: Size, mean: Scalar,
    swapRB: bool = false, crop: bool = false, ddepth: cint = 5): Mat {.
    importcpp: "cv::dnn::blobFromImage(@)", header: "<opencv2/dnn.hpp>".}
proc circle*(img: Mat, center: Point, radius: cint, color: Scalar, thickness: cint = 1): void {.importcpp: "cv::circle(@)", header: "<opencv2/imgproc.hpp>".}
proc rectangle*(img: Mat, pt1, pt2: Point, color: Scalar, thickness: cint = 1): void {.importcpp: "cv::rectangle(@)", header: "<opencv2/imgproc.hpp>".}
proc cvtColor*(src: Mat, dst: var Mat, code: cint): void {.importcpp: "cv::cvtColor(@)", header: "<opencv2/imgproc.hpp>".}
proc getDataBytePtr*(mat: Mat): ptr byte {.importcpp: "(uint8_t*)#.data", header: "<opencv2/core.hpp>".}

proc newVideoCapture*(index: cint): VideoCapture {.importcpp: "cv::VideoCapture(@)", header: "<opencv2/videoio.hpp>", constructor.}
proc newSize*(width: cint, height: cint): Size {.importcpp: "cv::Size(@)", header: "<opencv2/core.hpp>", constructor.}
proc newScalar*(v0: float64, v1: float64, v2: float64): Scalar {.importcpp: "cv::Scalar(@)", header: "<opencv2/core.hpp>", constructor.}
proc newMat*(): Mat {.importcpp: "cv::Mat()", header: "<opencv2/core.hpp>", constructor.}
proc newMat*(m: Mat, roi: Rect): Mat {.importcpp: "cv::Mat(@)", header: "<opencv2/core.hpp>", constructor.}
proc newPoint*(x: cint, y: cint): Point {.importcpp: "cv::Point(@)", header: "<opencv2/core.hpp>", constructor.}
proc newRect*(x: cint, y: cint, width: cint, height: cint): Rect {.importcpp: "cv::Rect(@)", header: "<opencv2/core.hpp>", constructor.}
proc getDataPtr*(mat: Mat): ptr float32 {.importcpp: "(float*)#.data", header: "<opencv2/core.hpp>".}
proc clone*(self: Mat): Mat {.importcpp: "#.clone()", header: "<opencv2/core.hpp>".}
proc newFaceDetectorYN*(model, config: cstring, inputSize: Size, 
                        scoreThreshold: float32 = 0.9f, 
                        nmsThreshold: float32 = 0.3f, 
                        topK: int32 = 5000, 
                        backendId: int32 = 0, 
                        targetId: int32 = 0): FaceDetectorYN {.importcpp: "cv::FaceDetectorYN::create(@)", header: "<opencv2/objdetect.hpp>".}
proc cols*(mat: Mat): cint {.importcpp: "#.cols", header: "<opencv2/core.hpp>".}
proc rows*(mat: Mat): cint {.importcpp: "#.rows", header: "<opencv2/core.hpp>".}
proc getAtFloat*(mat: Mat, row, col: cint): float32 {.importcpp: "#.at<float>(#, #)", header: "<opencv2/core.hpp>".}
proc detect*(self: FaceDetectorYN, image: Mat, faces: var Mat) {.importcpp: "#->detect(@)", header: "<opencv2/objdetect.hpp>".}
proc setInputSize*(self: FaceDetectorYN, inputSize: Size) {.importcpp: "#->setInputSize(@)", header: "<opencv2/objdetect.hpp>".}
