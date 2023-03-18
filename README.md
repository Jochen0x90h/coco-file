# CoCo File

File module for CoCo using transfer buffers.

## Import
Add coco-file/\<version> to your conanfile where version corresponds to the git tags

## Features
* Asynchronous reading and writing of files using transfer buffers
* Automatic increment of the file offset for streaming mode ("normal" file operation)
* Explicit file offset for each transfer buffer (comparable to pread/pwrite)

## Supported Platforms
* Native
  * Windows
  * Todo: Macos
  * Todo: Linux
