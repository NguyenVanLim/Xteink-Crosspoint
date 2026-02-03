#include "ImageBlock.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Serialization.h>

void ImageBlock::render(const GfxRenderer &renderer, int x, int y) const {
  FsFile f;
  if (SdMan.openFileForRead("IMB", bmpPath, f)) {
    Bitmap bmp(f);
    if (bmp.parseHeaders() == BmpReaderError::Ok) {
      renderer.drawBitmap(bmp, x, y, width, height);
    }
    f.close();
  } else {
    renderer.drawRect(x, y, width, height);
  }
}

bool ImageBlock::serialize(FsFile &file) const {
  serialization::writeString(file, bmpPath);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  return true;
}

std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile &file) {
  std::string bmpPath;
  uint16_t width;
  uint16_t height;

  serialization::readString(file, bmpPath);
  serialization::readPod(file, width);
  serialization::readPod(file, height);

  return std::unique_ptr<ImageBlock>(new ImageBlock(bmpPath, width, height));
}
