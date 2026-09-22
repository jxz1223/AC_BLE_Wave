# AC BLE Wave

STM32WB55VGY6 四路交流传感器 BLE 发射端固件。本仓库是四路传感器板已经联调验证的最终发射端源码：保留 STM32WB BLE 协议栈和 STM32CubeMX 工程配置，应用层仅包含 ADC 采样、四路独立自动调量程、归一化、组包和 BLE 通知发送。

## 当前发布版本

可直接烧录的最终固件位于 [firmware/AC_BLE_Wave_4CH_Center_Normalized_Final.hex](firmware/AC_BLE_Wave_4CH_Center_Normalized_Final.hex)。

- 目标 MCU：STM32WB55VGY6
- 采样方式：四路 ADC 轮询，每路 2000 S/s
- 模拟前端：四路独立 AD8231，增益为 1、2、4、8、16、32、64、128
- 自动调量程：每路独立判断，评估窗口约 120 ms
- 固件 SHA-256：`3A4889C367077CAF4AC28A7F7F4DC106BADBF4F993D0EAEC60B57DFF23CD0BD7`

## 工程结构

| 路径 | 内容 |
| --- | --- |
| `STM32_WPAN/App/sensor_app.c` | 四路采样、独立自动调量程、归一化与 BLE 载荷组包 |
| `STM32_WPAN/App/custom_app.c` | BLE GATT 通知对接 |
| `Core/`、`Drivers/`、`Middlewares/`、`Utilities/`、`STM32_WPAN/` | STM32Cube 固件、STM32WB BLE 协议栈和板级工程源码 |
| `BLE_VGY6.ioc` | STM32CubeMX 配置 |
| `firmware/` | 已验证的可烧录固件 |

## 硬件引脚对应

| 通道 | 仪表放大器 | ADC 输入 | 增益 A0 | 增益 A1 | 增益 A2 |
| --- | --- | --- | --- | --- | --- |
| CH1 | U4 | PC0 | PB13 | PA1 | PA2 |
| CH2 | U5 | PC1 | PA3 | PA4 | PA5 |
| CH3 | U6 | PC2 | PD7 | PA7 | PB3 |
| CH4 | U7 | PC3 | PC6 | PC7 | PC8 |

三个增益控制脚共同编码 AD8231 的八档增益。ADC 固定按 CH1、CH2、CH3、CH4 顺序轮询采样。

## 数据处理与 BLE 数据格式

每路均保存自己的 ADC 中心值和当前增益状态。12 位 ADC 原始数据按下式换算到等效增益为 1 的 19 位数据域：

```text
normalized = 262144 + (raw_adc - adc_center[channel]) * 128 / gain
```

因此，不论当前放大器增益是多少，静态中点均保持在约 `2048 * 128 = 262144`。BLE 应用层通知载荷为 246 字节：

```text
A6 6A | version = 02 | 76 个 24 位采样字
```

每个 24 位采样字按小端序发送，位定义如下：

```text
bit  0..18：归一化后的采样值
bit 19..20：ADC 通道编号（0..3）
bit 21..23：增益码（0..7，对应 1..128 倍）
```

每一个 BLE 通知帧含每路 19 个采样点，共 76 个采样字。

## 编译与烧录

1. 在 STM32CubeIDE 中将本目录作为已有 STM32 工程导入，或打开 `BLE_VGY6.ioc`。
2. 编译 `Debug` 配置。
3. 通过 ST-LINK 使用 STM32CubeProgrammer 烧录编译输出，或直接烧录 `firmware/` 中的最终 HEX 文件。

若 Windows 下 GNU Make 出现目录编码错误，请将工程克隆或导入到仅含 ASCII 字符的短路径，例如 `C:\work\AC_BLE_Wave`。

## 已验证功能

随仓库发布的 HEX 已完成实际硬件验证：四路 ADC 轮询、四路独立自动调量程、保持中心值的归一化、BLE 发射、现有 STM32WB 接收器转发，以及四路上位机解析显示。
