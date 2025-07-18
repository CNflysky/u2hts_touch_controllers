# U2HTS touch controllers
**Note**: If the `Controller name` is configured as `auto`, then every available slave address on I2C bus will be scanned, match the **first detected I2C slave** with an **integrated controller driver**, and initialise it. Be advised that **different controllers may have same I2C address** or **different drivers may register same I2C address**. If an incorrect controller match occurs, please manually configure the `Controller name`.  

**注意**: 如果将`控制器名`配置为`auto`，则固件会扫描i2c总线上的所有i2c从机地址，并将**第一个扫描到**的i2c从机与**已集成**的控制器驱动进行匹配并进行初始化。鉴于**不同的控制器可能拥有同一个i2c地址**或**不同的驱动可能注册了同一个i2c地址**，如果控制器匹配有误，请手动配置`控制器名`。    

| Vendor / 制造商 | Series / 系列 | Auto configuration / 自动配置 | Test / 测试 | Controller name /  控制器名 |
| --- | --- | --- | --- | --- |
| Goodix / 汇顶 | `GT9xx` | Y | GT5688 | `gt9xx` |
| Synaptics / 新思 | `RMI4-F11-I2C` | Y | S7300B | `rmi_f11` |
| Focaltech / 敦泰 | `FT54x6` | N | ft3168, ft5406 |  `ft54x6` |
| Hynitron / 海栎创 | `CST8xx` | N | cst816d | `cst8xx` |