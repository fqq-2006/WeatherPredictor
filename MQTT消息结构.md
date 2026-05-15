## MQTT消息结构
- clock/
  - equip1/
    - state
      - mode
      - page
      - rssi
      - voicestate
    - state/sensor
      - temp
      - humidity
    - state/city
    - set
      - [city,auto_state]
    - control
      - [page,updatetoday,updatefuture,voice,voicestate]
    - event
      - todayupdate
      - futureupdate
<br>
> 请求消息发送示例：
> ```json
> {"command" : ["shanghai",true]}
  **注意布尔状态值不要加引号**
> ```
> 空值要用 `null` 占位