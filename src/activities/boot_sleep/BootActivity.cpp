#include "BootActivity.h"

#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/CrossLarge.h"
#include "images/MrSlimLogo.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Two logos side-by-side (128x128 each)
  const int spacing = 20;
  const int totalW = 128 + spacing + 128;
  const int startX = (pageWidth - totalW) / 2;
  const int logoY = (pageHeight - 128) / 2 - 20;

  renderer.drawImage(CrossLarge, startX, logoY, 128, 128);
  renderer.drawImage(MrSlimLogo, startX + 128 + spacing, logoY, 128, 128);

  renderer.drawCenteredText(UI_10_FONT_ID, logoY + 128 + 20, "CrossPoint", true,
                            EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, logoY + 128 + 45, "BOOTING");

  // Move version display UP (was cut off at the bottom)
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 65, CROSSPOINT_VERSION);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 45,
                            "Mrslim Version By Lim Nguyen");
  renderer.displayBuffer();
}
