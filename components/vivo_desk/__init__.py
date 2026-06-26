import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import CONF_ID, STATE_CLASS_MEASUREMENT, UNIT_CENTIMETER

CODEOWNERS = ["@moellere"]
AUTO_LOAD = ["sensor"]
DEPENDENCIES = ["uart"]
MULTI_CONF = False

vivo_desk_ns = cg.esphome_ns.namespace("vivo_desk")
VivoDeskComponent = vivo_desk_ns.class_("VivoDeskComponent", cg.Component)

CONF_HEIGHT = "height"
CONF_HAND_UART = "hand_uart"
CONF_BRAIN_UART = "brain_uart"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VivoDeskComponent),
        cv.Required(CONF_HAND_UART): cv.use_id(uart.UARTComponent),
        cv.Required(CONF_BRAIN_UART): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_HEIGHT): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=1,
            icon="mdi:desk",
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    hand = await cg.get_variable(config[CONF_HAND_UART])
    brain = await cg.get_variable(config[CONF_BRAIN_UART])
    cg.add(var.set_hand_uart(hand))
    cg.add(var.set_brain_uart(brain))

    if CONF_HEIGHT in config:
        s = await sensor.new_sensor(config[CONF_HEIGHT])
        cg.add(var.set_height_sensor(s))
