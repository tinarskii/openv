import std/[math, streams]

const
  VERTEX_NUM     = 68
  ALPHA_ID_SIZE  = 219
  ALPHA_EXP_SIZE = 39

type
  Vec3      = array[3, float32]
  Mat3      = array[3, array[3, float32]]
  Landmark* = array[VERTEX_NUM, array[2, float32]]

# Load flat float32 array from .npy file (v1.0, no compression)
proc loadNpy*(path: string): seq[float32] =
  var f = openFileStream(path, fmRead)
  defer: f.close()
  # npy v1.0: 6 magic + 1 major + 1 minor + 2 header_len + header = 128 bytes total
  f.setPosition(128)
  var data: seq[float32]
  while not f.atEnd():
    var val: float32
    if f.readData(addr val, 4) == 4:
      data.add(val)
  return data

proc matMul3(a, b: Mat3): Mat3 =
  for i in 0..2:
    for j in 0..2:
      result[i][j] = 0
      for k in 0..2:
        result[i][j] += a[i][k] * b[k][j]

proc matVecT(m: Mat3, v: Vec3): Vec3 =
  for i in 0..2:
    result[i] = 0
    for j in 0..2:
      result[i] += m[j][i] * v[j]

proc projectLandmark*(
  output:     seq[float32],
  meanFace:   seq[float32],
  shapeBasis: seq[float32],
  blendShape: seq[float32]
): Landmark =
  # Parse output params
  var alphaId  = output[0..218]    # 219 values
  var alphaExp = output[219..257]  # 39 values
  let pitch  = output[258]
  let yaw    = output[259]
  let roll   = output[260]
  let tX_raw = output[261]
  let tY_raw = output[262]
  let f_raw  = output[263]

  # Denormalize
  for i in 0..<ALPHA_ID_SIZE:
    alphaId[i] *= 3.0'f32
  for i in 0..<ALPHA_EXP_SIZE:
    alphaExp[i] = alphaExp[i] * 0.5'f32 + 0.5'f32

  let pitchD = float32(pitch * PI / 2)
  let yawD   = float32(yaw   * PI / 2)
  let rollD  = float32(roll  * PI / 2)
  let tX = tX_raw * 60'f32
  let tY = tY_raw * 60'f32
  let tZ = 500'f32
  let f  = f_raw * 150'f32 + 450'f32

  # p_matrix (flip Y axis)
  let pMatrix: Mat3 = [
    [1'f32,                  0'f32,                 0'f32               ],
    [0'f32, float32(cos(-PI)),                       float32(-sin(-PI))  ],
    [0'f32, float32(sin(-PI)),                       float32(cos(-PI))   ]
  ]

  let rollMatrix: Mat3 = [
    [float32(cos(-rollD)), float32(-sin(-rollD)), 0'f32],
    [float32(sin(-rollD)), float32(cos(-rollD)),  0'f32],
    [0'f32,                0'f32,                 1'f32]
  ]

  let yawMatrix: Mat3 = [
    [float32(cos(-yawD)),  0'f32, float32(sin(-yawD)) ],
    [0'f32,                1'f32, 0'f32                ],
    [float32(-sin(-yawD)), 0'f32, float32(cos(-yawD)) ]
  ]

  let pitchMatrix: Mat3 = [
    [1'f32, 0'f32,                  0'f32                 ],
    [0'f32, float32(cos(pitchD)),  float32(-sin(-pitchD))],
    [0'f32, float32(sin(pitchD)),  float32(cos(pitchD)) ]
  ]

  # r = yaw @ pitch @ p @ roll
  let rMatrix = matMul3(yawMatrix, matMul3(pitchMatrix, matMul3(pMatrix, rollMatrix)))

  # Reconstruct vertices: meanFace + shapeBasis @ alphaId + blendShape @ alphaExp
  var vertices: array[VERTEX_NUM, Vec3]
  for v in 0..<VERTEX_NUM:
    for c in 0..2:
      var val = meanFace[v * 3 + c]
      for i in 0..<ALPHA_ID_SIZE:
        val += shapeBasis[(v * 3 + c) * ALPHA_ID_SIZE + i] * alphaId[i]
      for i in 0..<ALPHA_EXP_SIZE:
        val += blendShape[(v * 3 + c) * ALPHA_EXP_SIZE + i] * alphaExp[i]
      vertices[v][c] = val

  # Apply rotation
  for v in 0..<VERTEX_NUM:
    vertices[v] = matVecT(rMatrix, vertices[v])

  # Apply translation + project to 2D
  for v in 0..<VERTEX_NUM:
    vertices[v][0] += tX
    vertices[v][1] += tY
    vertices[v][2] += tZ
    result[v][0] = vertices[v][0] * f / tZ
    result[v][1] = vertices[v][1] * f / tZ

proc transformLandmark*(
  lmk:               var Landmark,
  x0, y0, x1, y1:   int,
  resizedH, resizedW: int
) =
  let width  = float32(x1 - x0) 
  let height = float32(y1 - y0)
  
  for v in 0..<VERTEX_NUM:
    lmk[v][0] = (lmk[v][0] + 64.0) * (width / 128.0) + float32(x0)
    lmk[v][1] = (lmk[v][1] + 64.0) * (height / 128.0) + float32(y0)