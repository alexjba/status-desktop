## Feeds NimQrScan scanners from NimQtCamera frame taps.
##
## nim-qrscan doesn't know where frames come from; the host decides. Here a
## scanner's "frame source" is a NimQtCamera FrameTap handle, so in QML:
##   QrScanner { frameSource: frameTap.handle }
## On Android it is a NativeQrCodeCapture frameSource instead (nim-qtcamera,
## i.e. QtMultimedia, is not linked there).
## Used by the app (nim_status_client.nim) and by Storybook's Nim library.

import qrscan/qml as scan

when defined(android):
  # StatusQ's NativeQrCodeCapture (CameraX): its frameSource handles; frames
  # arrive on the GUI thread.
  type NativeConsumer = proc(user: int, data: ptr UncheckedArray[byte],
                             width, height, rowStride: cint) {.cdecl, gcsafe, raises: [].}

  {.push dynlib: "", importc.}
  proc statusq_qrcam_set_consumer(source: cint, consumer: NativeConsumer, user: int) {.cdecl.}
  proc statusq_qrcam_arm(source: cint) {.cdecl.}
  {.pop.}

  proc onNativeFrame(user: int, data: ptr UncheckedArray[byte],
                     width, height, rowStride: cint) {.cdecl, gcsafe, raises: [].} =
    scan.deliverFrame(user, toOpenArray(data, 0, int(height) * int(rowStride) - 1),
                      int(width), int(height), int(rowStride))
else:
  import qtcamera/types as camtypes
  import qtcamera/qml as cam

proc cameraFrames*(): scan.FrameSource =
  when defined(android):
    scan.FrameSource(
      attach: proc(source, scanner: int) {.gcsafe, raises: [].} =
        statusq_qrcam_set_consumer(cint(source), onNativeFrame, scanner),
      requestNext: proc(source: int) {.gcsafe, raises: [].} =
        statusq_qrcam_arm(cint(source)),
      detach: proc(source: int) {.gcsafe, raises: [].} =
        statusq_qrcam_set_consumer(cint(source), nil, 0))
  else:
    scan.FrameSource(
      attach: proc(source, scanner: int) {.gcsafe, raises: [].} =
        cam.setTapConsumer(source, proc(f: sink camtypes.GrayFrame) {.gcsafe, raises: [].} =
          scan.deliverFrame(scanner, f.data, f.width, f.height, f.width)),
      requestNext: proc(source: int) {.gcsafe, raises: [].} =
        cam.armTap(source),
      detach: proc(source: int) {.gcsafe, raises: [].} =
        cam.setTapConsumer(source, nil))
