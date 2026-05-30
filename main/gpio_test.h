#pragma once

// Modo de prueba de pinout — cicla los GPIOs del header J8 para que el
// operador pueda identificar con multímetro qué pin físico corresponde a
// cada GPIO. Nunca retorna; el firmware normal NO se ejecuta mientras
// GPIO_TEST_MODE esté en 1 (ver app_config.h).
//
// Para volver al firmware normal, poner GPIO_TEST_MODE=0 en app_config.h
// y re-flashear.

#ifdef __cplusplus
extern "C" {
#endif

void gpio_test_loop(void);

#ifdef __cplusplus
}
#endif
