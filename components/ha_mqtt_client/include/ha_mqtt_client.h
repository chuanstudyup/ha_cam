#ifndef _HA_MQTT_CLIENT_H_
#define _HA_MQTT_CLIENT_H_ 

// #define CONFIG_ENABLE_MQTT 1

#ifdef CONFIG_ENABLE_MQTT

void mqtt_app_start();
void mqtt_broadcast_discovery();
void mqtt_publish_data();

#endif

#endif
