# NYCU UAV 2023 Teststand 2

一個高度自動化的馬達拉力測試架！

這個 repo 存有控制器韌體與電腦端 GUI。

## 控制器韌體開發環境建置

控制器韌體是利用 PlatformIO 進行開發，專案的設定檔為 `platformio.ini`。

## 電腦端 GUI 環境建置

請先確保電腦已安裝 Python，並且可以在終端機中運行 Python。

```
pip install PySimpleGUI pyserial pandas matplotlib
python gui.py
```

你可以在 GUI 中安全地設置串列通訊埠、鎖定量測參數、初始化系統、進行測量、視覺化數據。

### 安全量測操作手續

1. 搖晃測試架檢測整體結構是否穩固，確認螺旋槳是否鎖緊，將安全開關轉至 ON。
2. 將測試架控制器以足夠長度的數據線與電腦連接，確認該控制器於電腦端可見。
3. 確認系統周圍淨空、非人員撤離，動力電負責人大喊「上電」後將動力電接入系統。
4. 確認動力電負責人撤離至安全區後，電腦端介面負責人進行系統初始化。
5. 確認系統正常初始化後，電腦端介面負責人進行測量，此時，安全開關負責人需時刻注意系統狀態。
6. 量測結束後，量測數據與圖表將會自動回傳、處理並顯示。

### 圖表與資料判讀

生成的圖表分別為 Power, Thrust, Current, Efficiency （y 軸）對 RPM, Throttle （x 軸）共 8 組。

### 判讀前

需先檢查測得數據的單位是否正確

- Power: W
- Thrust: kg
- Current: A
- Efficiency: kg/w
- Throttle: %

### 判讀

- 除 Efficiency 測得的數據為遞減，其餘皆為遞增。
- 各數值上限是否有超過馬達規格。

## 系統構成部件及規格

### 需外購的模組列表

- ESP32
- Load cell
- HX711 load cell amplifier
- IR obstacle avoidance sensor

### 量測數值上限

- 電流 < 120 A
- 電壓 < 30 V
- 推力 < 20 kg

### 槳葉尺寸上限

推薦的最大槳葉尺寸為 21 吋。
葉片在極限狀態最大能裝到 22 吋，但可能撞到鐵絲安全網。
