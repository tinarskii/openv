import std/math
when defined(windows):
  import std/winlean
else:
  import std/posix
import onnx_rt, landmark, types

const CAP_PROP_FRAME_WIDTH = 3
const CAP_PROP_FRAME_HEIGHT = 4
const CAP_PROP_BUFFERSIZE = 38
const DEBUG_DRAW = false
const SHOW_PREVIEW = false
const DETECT_EVERY_N_FRAMES = 3

when defined(windows):
  const SHM_NAME = "Local\\face_params"
else:
  const SHM_NAME = "/face_params"

when defined(windows):
  var shmHandle: Handle = 0
else:
  var shmFd: cint = -1
var params: ptr FaceParams = nil

proc openSharedParams(): bool =
  when defined(windows):
    let shmNameWide = newWideCString(SHM_NAME)
    shmHandle = createFileMappingW(
      INVALID_HANDLE_VALUE,
      nil,
      PAGE_READWRITE.DWORD,
      0.DWORD,
      DWORD(sizeof(FaceParams)),
      cast[pointer](shmNameWide)
    )
    if shmHandle == 0:
      return false
    let mapped = mapViewOfFileEx(
      shmHandle,
      FILE_MAP_WRITE.DWORD,
      0.DWORD,
      0.DWORD,
      WinSizeT(sizeof(FaceParams)),
      nil
    )
    if mapped == nil:
      discard closeHandle(shmHandle)
      shmHandle = 0
      return false
    params = cast[ptr FaceParams](mapped)
    true
  else:
    shmFd = shm_open(SHM_NAME, O_CREAT or O_RDWR, 0o666)
    if shmFd < 0:
      return false
    discard ftruncate(shmFd, sizeof(FaceParams).Off)
    let mapped = mmap(nil, sizeof(FaceParams), PROT_WRITE, MAP_SHARED, shmFd, 0)
    if mapped == cast[pointer](-1):
      discard close(shmFd)
      shmFd = -1
      return false
    params = cast[ptr FaceParams](mapped)
    true

proc closeSharedParams() =
  when defined(windows):
    if params != nil:
      discard flushViewOfFile(cast[pointer](params), DWORD(sizeof(FaceParams)))
      discard unmapViewOfFile(cast[pointer](params))
      params = nil
    if shmHandle != 0:
      discard closeHandle(shmHandle)
      shmHandle = 0
  else:
    if params != nil:
      discard munmap(params, sizeof(FaceParams))
      params = nil
    if shmFd >= 0:
      discard close(shmFd)
      shmFd = -1
    discard shm_unlink(SHM_NAME)

proc getDistance(p1, p2: array[2, float32]): float32 =
  sqrt((p1[0] - p2[0])^2 + (p1[1] - p2[1])^2)

proc eyeAspectRatio(lmk: openArray[array[2, float32]], p0, p1, p2, p3, p4, p5: int): float32 =
  let horizontal = max(1.0'f32, getDistance(lmk[p0], lmk[p3]))
  let vertical = getDistance(lmk[p1], lmk[p5]) + getDistance(lmk[p2], lmk[p4])
  vertical / (2.0'f32 * horizontal)

proc normalizeEyeOpen(ear: float32): float32 =
  # Wider calibration + gentle curve to avoid over-sensitive blink detection.
  const eyeClosedEar = 0.08'f32
  const eyeOpenEar = 0.34'f32
  let t = clamp((ear - eyeClosedEar) / (eyeOpenEar - eyeClosedEar), 0.0'f32, 1.0'f32)
  pow(t, 0.55'f32)

proc matToFloat32Seq(mat: Mat): seq[float32] =
  let totalElements = 1 * 3 * 128 * 128
  result = newSeq[float32](totalElements)
  let dataPtr = getDataPtr(mat)
  if dataPtr != nil:
    copyMem(addr result[0], dataPtr, totalElements * sizeof(float32))
  else:
    echo "Error: Could not get data pointer from Mat."

if isMainModule:
  if not openSharedParams():
    echo "Could not create shared memory channel"
    quit(1)
  defer: closeSharedParams()
  # var client: WebSocket = waitFor newWebSocket("ws://localhost:4622")
  var cap: VideoCapture = newVideoCapture(0.cint)
  let model = loadModel("./model/model.onnx")
  let meanFace   = loadNpy("assets/meanFace.npy")
  let shapeBasis = loadNpy("assets/shapeBasis.npy")
  let blendShape = loadNpy("assets/blendShape.npy")
  
  var smoothX0, smoothY0, smoothX1, smoothY1: float32
  var smoothYaw, smoothPitch, smoothRoll, smoothMouth, smoothMouthForm: float32
  var smoothEyeBallX, smoothEyeBallY: float32
  var smoothEyeOpenL, smoothEyeOpenR, smoothBrowL, smoothBrowR: float32
  
  const alphaBox: float32 = 0.45
  const alphaAngle: float32 = 0.60

  if not cap.isOpened():
    echo "Could not open camera"
    quit(1)

  discard cap.set(CAP_PROP_FRAME_WIDTH, 640.0)
  discard cap.set(CAP_PROP_FRAME_HEIGHT, 480.0)
  discard cap.set(CAP_PROP_BUFFERSIZE, 1.0)

  let frameWidth  = cap.get(CAP_PROP_FRAME_WIDTH).int
  let frameHeight = cap.get(CAP_PROP_FRAME_HEIGHT).int
  let targetSize  = newSize(128, 128)
  let mean        = newScalar(0.0, 0.0, 0.0)
  let shape       = @[1.int64, 3, 128, 128]
  let yunet = newFaceDetectorYN(
    "./model/yunet.onnx".cstring,
    "".cstring, 
    newSize(frameWidth.cint, frameHeight.cint),
    0.9.float32,
    0.3.float32,
    64.cint
  )

  var frame = newMat()
  var blob  = newMat()
  var faces = newMat()
  var frameCount = 0
  var hasFace = false
  var missedDetections = 0

  if SHOW_PREVIEW:
    namedWindow("Frame", 1)

  while true:
    frameCount.inc
    if not cap.read(frame):
      echo "Could not read frame"
      break
    if frame.empty():
      echo "Empty frame"
      break

    var x0, y0, w, h: int
    let shouldDetect = (frameCount mod DETECT_EVERY_N_FRAMES == 0) or (not hasFace)
    if shouldDetect:
      yunet.setInputSize(newSize(frame.cols(), frame.rows()))
      yunet.detect(frame, faces)
      if faces.rows() > 0:
        x0 = faces.getAtFloat(0, 0).int
        y0 = faces.getAtFloat(0, 1).int
        w  = faces.getAtFloat(0, 2).int
        h  = faces.getAtFloat(0, 3).int
        hasFace = true
        missedDetections = 0
      else:
        missedDetections.inc
        if missedDetections >= 5:
          hasFace = false
          if SHOW_PREVIEW:
            imshow("Frame", frame)
            if waitKey(1) == 27: break
          continue
        x0 = smoothX0.int
        y0 = smoothY0.int
        w = max(1, smoothX1.int - smoothX0.int)
        h = max(1, smoothY1.int - smoothY0.int)
    else:
      x0 = smoothX0.int
      y0 = smoothY0.int
      w = max(1, smoothX1.int - smoothX0.int)
      h = max(1, smoothY1.int - smoothY0.int)

    let x1 = x0 + w
    let y1 = y0 + h

    smoothX0 = (alphaBox * x0.float32) + ((1.0 - alphaBox) * smoothX0)
    smoothY0 = (alphaBox * y0.float32) + ((1.0 - alphaBox) * smoothY0)
    smoothX1 = (alphaBox * x1.float32) + ((1.0 - alphaBox) * smoothX1)
    smoothY1 = (alphaBox * y1.float32) + ((1.0 - alphaBox) * smoothY1)

    let padW = (w.float32 * 0.15).int
    let padH = (h.float32 * 0.15).int

    let cx0 = max(0, smoothX0.int - padW)
    let cy0 = max(0, smoothY0.int - padH)
    let cx1 = min(frameWidth - 1, smoothX1.int + padW)
    let cy1 = min(frameHeight - 1, smoothY1.int + padH)

    let faceW = cx1 - cx0
    let faceH = cy1 - cy0

    if faceW <= 10 or faceH <= 10:
      continue

    let centerX = (cx0 + cx1) div 2
    let centerY = (cy0 + cy1) div 2
    let side = max(cx1 - cx0, cy1 - cy0) div 2

    let sqX0 = max(0, centerX - side)
    let sqY0 = max(0, centerY - side)
    let sqX1 = min(frameWidth - 1, centerX + side)
    let sqY1 = min(frameHeight - 1, centerY + side)
    let sqW = (sqX1 - sqX0).cint
    let sqH = (sqY1 - sqY0).cint

    if sqW <= 0 or sqH <= 0:
      continue

    blob = blobFromImage(newMat(frame, newRect(sqX0.cint, sqY0.cint, sqW, sqH)), 1.0/255.0, targetSize, mean, swapRB=true, crop=false, ddepth=5)
    let inputData = matToFloat32Seq(blob)
    let input  = newInputTensor(inputData, shape)
    let output = model.run(input, inputName="image", outputName="parameters_3dmm")

    let rawPitch = output.data[258] * PI / 2
    let rawYaw   = output.data[259] * PI / 2
    let rawRoll  = output.data[260] * PI / 2

    var lmk = projectLandmark(output.data, meanFace, shapeBasis, blendShape)
    transformLandmark(lmk, sqX0, sqY0, sqX1, sqY1, 128, 128)
    let rawMouth = getDistance(lmk[62], lmk[66]) / 15.0
    let mouthWidth = getDistance(lmk[48], lmk[54])
    let faceWidth = max(1.0'f32, getDistance(lmk[0], lmk[16]))
    let faceHeight = max(1.0'f32, getDistance(lmk[27], lmk[8]))
    let rawMouthForm = clamp(((mouthWidth / faceWidth) - 0.32'f32) * 8.0'f32, -1.0'f32, 1.0'f32)
    let earL = eyeAspectRatio(lmk, 36, 37, 38, 39, 40, 41)
    let earR = eyeAspectRatio(lmk, 42, 43, 44, 45, 46, 47)
    let rawEyeOpenL = normalizeEyeOpen(earL)
    let rawEyeOpenR = normalizeEyeOpen(earR)
    let eyeCenterLx = (lmk[36][0] + lmk[39][0]) * 0.5'f32
    let eyeCenterLy = (lmk[37][1] + lmk[38][1] + lmk[40][1] + lmk[41][1]) * 0.25'f32
    let eyeCenterRx = (lmk[42][0] + lmk[45][0]) * 0.5'f32
    let eyeCenterRy = (lmk[43][1] + lmk[44][1] + lmk[46][1] + lmk[47][1]) * 0.25'f32
    let eyesCenterX = (eyeCenterLx + eyeCenterRx) * 0.5'f32
    let eyesCenterY = (eyeCenterLy + eyeCenterRy) * 0.5'f32
    let noseX = lmk[30][0]
    let noseY = lmk[30][1]
    let rawEyeBallFromLmkX = clamp((noseX - eyesCenterX) / max(1.0'f32, faceWidth * 0.18'f32), -1.0'f32, 1.0'f32)
    let rawEyeBallFromLmkY = clamp((noseY - eyesCenterY) / max(1.0'f32, faceHeight * 0.22'f32), -1.0'f32, 1.0'f32)
    # Drive gaze primarily from head pose (reliable), with small landmark correction.
    let rawEyeBallFromPoseX = clamp(rawYaw * 1.8'f32, -1.0'f32, 1.0'f32)
    let rawEyeBallFromPoseY = clamp(rawPitch * 1.6'f32, -1.0'f32, 1.0'f32)
    let rawEyeBallX = clamp(rawEyeBallFromPoseX * 0.8'f32 + rawEyeBallFromLmkX * 0.2'f32, -1.0'f32, 1.0'f32)
    let rawEyeBallY = clamp(rawEyeBallFromPoseY * 0.8'f32 + rawEyeBallFromLmkY * 0.2'f32, -1.0'f32, 1.0'f32)
    let browLeftY = (lmk[19][1] + lmk[20][1]) * 0.5'f32
    let browRightY = (lmk[23][1] + lmk[24][1]) * 0.5'f32
    let eyeLeftY = (lmk[37][1] + lmk[38][1] + lmk[40][1] + lmk[41][1]) * 0.25'f32
    let eyeRightY = (lmk[43][1] + lmk[44][1] + lmk[46][1] + lmk[47][1]) * 0.25'f32
    let rawBrowL = clamp(((eyeLeftY - browLeftY) / faceHeight - 0.09'f32) * 12.0'f32, -1.0'f32, 1.0'f32)
    let rawBrowR = clamp(((eyeRightY - browRightY) / faceHeight - 0.09'f32) * 12.0'f32, -1.0'f32, 1.0'f32)

    smoothPitch = (alphaAngle * rawPitch) + ((1.0 - alphaAngle) * smoothPitch)
    smoothYaw   = (alphaAngle * rawYaw)   + ((1.0 - alphaAngle) * smoothYaw)
    smoothRoll  = (alphaAngle * rawRoll)  + ((1.0 - alphaAngle) * smoothRoll)
    smoothMouth = (alphaAngle * rawMouth) + ((1.0 - alphaAngle) * smoothMouth)
    smoothMouthForm = (alphaAngle * rawMouthForm) + ((1.0 - alphaAngle) * smoothMouthForm)
    smoothEyeBallX = (alphaAngle * rawEyeBallX) + ((1.0 - alphaAngle) * smoothEyeBallX)
    smoothEyeBallY = (alphaAngle * rawEyeBallY) + ((1.0 - alphaAngle) * smoothEyeBallY)
    smoothEyeOpenL = (alphaAngle * rawEyeOpenL) + ((1.0 - alphaAngle) * smoothEyeOpenL)
    smoothEyeOpenR = (alphaAngle * rawEyeOpenR) + ((1.0 - alphaAngle) * smoothEyeOpenR)
    smoothBrowL = (alphaAngle * rawBrowL) + ((1.0 - alphaAngle) * smoothBrowL)
    smoothBrowR = (alphaAngle * rawBrowR) + ((1.0 - alphaAngle) * smoothBrowR)
      
    params.version.inc
    params.angleX = clamp(smoothYaw * (180.0/PI), -30.0, 30.0)
    params.angleY = clamp(smoothPitch * (180.0/PI), -30.0, 30.0)
    params.angleZ = clamp(smoothRoll * (180.0/PI), -30.0, 30.0)
    params.mouthOpen = clamp(smoothMouth, 0.0, 1.0)
    params.mouthForm = clamp(smoothMouthForm, -1.0, 1.0)
    params.eyeBallX = clamp(smoothEyeBallX, -1.0, 1.0)
    params.eyeBallY = clamp(smoothEyeBallY, -1.0, 1.0)
    params.eyeOpenL = clamp(smoothEyeOpenL, 0.0, 1.0)
    params.eyeOpenR = clamp(smoothEyeOpenR, 0.0, 1.0)
    params.browL = clamp(smoothBrowL, -1.0, 1.0)
    params.browR = clamp(smoothBrowR, -1.0, 1.0)
    params.version.inc

    if DEBUG_DRAW:
      rectangle(frame, newPoint(sqX0.cint, sqY0.cint), newPoint(sqX1.cint, sqY1.cint), newScalar(255, 0, 0), 2.cint)
      for i in 0..<68:
        let x = lmk[i][0].int
        let y = lmk[i][1].int
        circle(frame, newPoint(x.cint, y.cint), 2.cint, newScalar(0, 0, 255), -1.cint)

    if SHOW_PREVIEW:
      imshow("Frame", frame)
      if waitKey(1) == 27: break

  cap.release()
  if SHOW_PREVIEW:
    destroyAllWindows()
