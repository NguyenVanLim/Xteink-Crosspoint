# mrslim Firmware - Custom cho X-Teink X4

Firmware custom cho **Xteink X4** e-paper display reader, dựa trên nền **Crosspoint 0.16.0 dev**.
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

mrslim Firmware là một bản custom được chế lại từ CrossPoint Reader nhằm mang lại trải nghiệm đọc sách EPUB tốt hơn với hỗ trợ đầy đủ Tiếng Việt và nhiều tính năng bổ sung.

![](./docs/images/cover.jpg)

> [!WARNING]
> **CẢNH BÁO QUAN TRỌNG:**
>
> - **Miễn trừ trách nhiệm:** Vì là bản vọc cá nhân, em xin phép KHÔNG CHỊU TRÁCH NHIỆM nếu máy gặp lỗi hoặc có vấn đề gì xảy ra khi anh em cài đặt. Anh em cân nhắc kỹ trước khi lên nhé!
> - **Rủi ro:** Đây là bản em tự vọc cá nhân và chưa test kỹ, có thể phát sinh lỗi trong quá trình sử dụng.
> - **Nhược điểm:** Do em nhồi nhét thêm tính năng nên giao diện sẽ lag hơn bản gốc một chút.
>
> **Upstream Source:** [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)

## Giới thiệu

Chào mọi người, sau mấy ngày rảnh rỗi vọc vạch, em có chế lại chút firmware cho X-Teink X4 dựa trên nền Crosspoint 0.16.0 dev. Em chia sẻ lên đây cho anh em nào thích trải nghiệm cái mới.

## ✨ Những tính năng em đã thêm vào:

- **Update giao diện Tiếng Việt:** Dễ nhìn, dễ dùng hơn hẳn cho người Việt.
- **Update thanh tiến trình đọc:** Theo dõi tiến độ đọc sách trực quan.
- **Giao diện trực quan:** Nhìn hiện đại và thoáng hơn bản cũ.
- **Tủ sách hiện Cover:** Đã hiện được bìa sách cho đẹp đội hình.
- **Hỗ trợ ảnh trong EPUB:** Xem được ảnh minh họa trong sách ngon lành.

## 💻 Góc hỗ trợ & Giao lưu:

### Về Firmware
Anh em muốn thêm chức năng gì cứ comment bên dưới nhé, em sẽ tìm hiểu để vọc vạch thêm cho các bản sau.

### Về Website
Tiện đây em cũng đang nhận code Website cho cá nhân và doanh nghiệp. Anh chị em nào cần làm web bán hàng, landing page hay web công ty thì cứ ới em nhé. Em hỗ trợ nhiệt tình, giá "người nhà" cho anh em ạ!

## Motivation

## Motivation

E-paper devices are fantastic for reading, but most commercially available readers are closed systems with limited 
customisation. The **Xteink X4** is an affordable, e-paper device, however the official firmware remains closed.
CrossPoint exists partly as a fun side-project and partly to open up the ecosystem and truely unlock the device's
potential.

mrslim Firmware aims to:
* Provide a **fully open-source alternative** to the official firmware.
* Offer a **document reader** capable of handling EPUB content on constrained hardware.
* Support **customisable font, layout, and display** options.
* Run purely on the **Xteink X4 hardware**.

This project is **not affiliated with Xteink**; it's built as a community project.

## Features & Usage

- [x] EPUB parsing and rendering (EPUB 2 and EPUB 3)
- [x] Image support within EPUB
- [x] Saved reading position
- [x] File explorer with file picker
  - [x] Basic EPUB picker from root directory
  - [x] Support nested folders
  - [ ] EPUB picker with cover art
- [x] Custom sleep screen
  - [x] Cover sleep screen
- [x] Wifi book upload
- [x] Wifi OTA updates
- [x] Configurable font, layout, and display options
  - [ ] User provided fonts
  - [ ] Full UTF support
- [x] Screen rotation

Multi-language support: Read EPUBs in various languages, including English, Spanish, French, German, Italian, Portuguese, Russian, Ukrainian, Polish, Swedish, Norwegian, [and more](./USER_GUIDE.md#supported-languages).

See [the user guide](./USER_GUIDE.md) for instructions on operating CrossPoint. 

## Installing

### Web (latest firmware)

1. Connect your Xteink X4 to your computer via USB-C
2. Go to https://xteink.dve.al/ and click "Flash CrossPoint firmware"

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Web (specific firmware version)

1. Connect your Xteink X4 to your computer via USB-C
2. Download the `firmware.bin` file from the release of your choice via the [releases page](https://github.com/daveallie/crosspoint-reader/releases)
3. Go to https://xteink.dve.al/ and flash the firmware file using the "OTA fast flash controls" section

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Manual

See [Development](#development) below.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

CrossPoint uses PlatformIO for building and flashing the firmware. To get started, clone the repository:

```
git clone --recursive https://github.com/daveallie/crosspoint-reader

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Flashing your device

Connect your Xteink X4 to your computer via USB-C and run the following command.

```sh
pio run --target upload
```

## Internals

CrossPoint Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only
has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based
on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the 
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:


```
.crosspoint/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
└── epub_189013891/
```

Deleting the `.crosspoint` directory will clear the entire cache. 

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book
file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Contributing

Contributions are very welcome!

If you're looking for a way to help out, take a look at the [ideas discussion board](https://github.com/daveallie/crosspoint-reader/discussions/categories/ideas).
If there's something there you'd like to work on, leave a comment so that we can avoid duplicated effort.

### To submit a contribution:

1. Fork the repo
2. Create a branch (`feature/dithering-improvement`)
3. Make changes
4. Submit a PR

---

CrossPoint Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.

Huge shoutout to [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader), which was a project I took a lot of inspiration from as I
was making CrossPoint.
