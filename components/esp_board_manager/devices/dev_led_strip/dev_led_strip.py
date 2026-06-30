# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LED strip device config parser
VERSION = 'v1.0.0'

VALID_SUB_TYPES = ['rmt', 'spi']

COLOR_COMPONENT_FORMAT_MAP = {
    'LED_STRIP_COLOR_COMPONENT_FMT_GRB': 'LED_PIXEL_FORMAT_GRB',
    'LED_STRIP_COLOR_COMPONENT_FMT_GRBW': 'LED_PIXEL_FORMAT_GRBW',
}

UNSUPPORTED_COLOR_COMPONENT_FORMATS = {
    'LED_STRIP_COLOR_COMPONENT_FMT_RGB',
    'LED_STRIP_COLOR_COMPONENT_FMT_RGBW',
}


def _parse_pixel_format(config: dict) -> str:
    value = config.get('led_pixel_format', config.get('color_component_format', 'LED_PIXEL_FORMAT_GRB'))
    if value in UNSUPPORTED_COLOR_COMPONENT_FORMATS:
        raise ValueError(
            f'{value} has no exact led_pixel_format equivalent in the current led_strip driver'
        )
    return COLOR_COMPONENT_FORMAT_MAP.get(value, value)


def get_includes() -> list:
    """Return required include headers for LED strip device"""
    return [
        'dev_led_strip.h',
    ]


def _parse_strip_config(config: dict) -> dict:
    """Parse common led_strip_config_t fields"""
    return {
        'strip_gpio_num': int(config.get('strip_gpio_num', -1)),
        'max_leds': int(config.get('max_leds', 1)),
        'led_pixel_format': _parse_pixel_format(config),
        'led_model': config.get('led_model', 'LED_MODEL_WS2812'),
        'flags': {
            'invert_out': config.get('invert_out', config.get('flags', {}).get('invert_out', False)),
        },
    }


def _parse_logical_lights(config: dict, max_leds: int) -> list:
    """Parse optional logical light ranges for one physical LED strip"""
    logical_lights = config.get('logical_lights', [])
    if logical_lights is None:
        logical_lights = []
    if not isinstance(logical_lights, list):
        raise ValueError('logical_lights must be a list')
    if len(logical_lights) > 32:
        raise ValueError('logical_lights supports at most 32 entries')

    parsed = []
    for index, item in enumerate(logical_lights):
        if not isinstance(item, dict):
            raise ValueError(f'logical_lights[{index}] must be a mapping')

        first_led = int(item.get('first_led', item.get('index', index)))
        led_count = int(item.get('led_count', item.get('count', 1)))
        if first_led < 0:
            raise ValueError(f'logical_lights[{index}] first_led must be >= 0')
        if led_count <= 0:
            raise ValueError(f'logical_lights[{index}] led_count must be > 0')
        if first_led >= max_leds or led_count > max_leds - first_led:
            raise ValueError(
                f'logical_lights[{index}] range {first_led}+{led_count} exceeds max_leds {max_leds}'
            )

        parsed.append({
            'id': item.get('id', item.get('name', None)),
            'title': item.get('title', item.get('label', None)),
            'first_led': first_led,
            'led_count': led_count,
        })

    return parsed


def _parse_rmt_sub_config(config: dict) -> dict:
    """Parse led_strip_rmt_config_t fields"""
    rmt_config = config.get('rmt', config.get('rmt_config', {}))
    return {
        'rmt_config': {
            'clk_src': rmt_config.get('clk_src', 'RMT_CLK_SRC_DEFAULT'),
            'resolution_hz': int(rmt_config.get('resolution_hz', 10000000)),
            'mem_block_symbols': int(rmt_config.get('mem_block_symbols', 0)),
            'flags': {
                'with_dma': rmt_config.get('with_dma', rmt_config.get('flags', {}).get('with_dma', False)),
            },
        },
    }


def _parse_spi_sub_config(config: dict) -> dict:
    """Parse led_strip_spi_config_t fields"""
    spi_config = config.get('spi', config.get('spi_config', {}))
    return {
        'spi_config': {
            'clk_src': spi_config.get('clk_src', 'SPI_CLK_SRC_DEFAULT'),
            'spi_bus': spi_config.get('spi_bus', 'SPI2_HOST'),
            'flags': {
                'with_dma': spi_config.get('with_dma', spi_config.get('flags', {}).get('with_dma', True)),
            },
        },
    }


def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse LED strip device configuration from YAML"""
    config = full_config.get('config', {})
    chip = full_config.get('chip', config.get('chip', 'led_strip'))
    sub_type = full_config.get('sub_type')

    if not sub_type:
        raise ValueError(f"LED strip device '{name}' is missing required 'sub_type' field")

    if sub_type not in VALID_SUB_TYPES:
        raise ValueError(
            f"LED strip device '{name}' has invalid 'sub_type' value '{sub_type}'. "
            f'Must be one of: {VALID_SUB_TYPES}'
        )

    strip_config = _parse_strip_config(config)

    if strip_config['strip_gpio_num'] < 0:
        raise ValueError(f"LED strip device '{name}' requires valid strip_gpio_num")
    if strip_config['max_leds'] <= 0:
        raise ValueError(f"LED strip device '{name}' requires max_leds > 0")
    logical_lights = _parse_logical_lights(config, strip_config['max_leds'])

    if sub_type == 'rmt':
        sub_cfg = {'rmt': _parse_rmt_sub_config(config)}
    elif sub_type == 'spi':
        sub_cfg = {'spi': _parse_spi_sub_config(config)}
    else:
        raise ValueError(f'Unsupported LED strip sub_type: {sub_type}')

    struct_init = {
        'name': name,
        'chip': chip,
        'sub_type': sub_type,
        'strip_config': strip_config,
        'logical_light_count': len(logical_lights),
        'sub_cfg': sub_cfg,
    }
    if logical_lights:
        struct_init['logical_lights'] = logical_lights

    return {
        'struct_type': 'dev_led_strip_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': struct_init,
    }
