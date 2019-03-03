/*
 * Copyright (C) 2019 Dmitry Adzhiev dmitry.adjiev@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/select.h>
#include <time.h>
#include <hardware/gps.h>
#include <cutils/properties.h>

#ifdef PROP_GPS_DEBUG
#define  LOG_TAG  "propgps"
#include <cutils/log.h>
#  define  TRACE(...)   ALOGD(__VA_ARGS__)
#else
#  define  TRACE(...)   ((void)0)
#endif

#define GPS_LAT_PROP "gps.latitude"
#define GPS_LONG_PROP "gps.longitude"

struct prop_gps_context {
    float accuracy;
    GpsCallbacks *cb;
    bool initialized;
    GpsLocation location;
    pthread_mutex_t mutex;
    volatile bool stopped;
    pthread_t thread;
};

static struct prop_gps_context _prop_gps_ctx[1];

static double now_ms(void) {
    struct timespec res;
    clock_gettime(CLOCK_REALTIME, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;

}

// gps thread

static void prop_gps_thread_main(void *data)
{
    struct timeval tv;
    memset(&tv, 0, sizeof(struct timeval));
    struct prop_gps_context *ctx = (struct prop_gps_context*) data;
    char latitude[PROPERTY_VALUE_MAX];
    char longitude[PROPERTY_VALUE_MAX];
    memset(latitude, 0, PROPERTY_VALUE_MAX);
    memset(longitude, 0, PROPERTY_VALUE_MAX);
    bool active = !ctx->stopped;
    ctx->location.flags = GPS_LOCATION_HAS_LAT_LONG | GPS_LOCATION_HAS_ACCURACY;

    while (active) {
        if (property_get(GPS_LAT_PROP, latitude, "0")) {
            ctx->location.latitude = strtod(latitude, NULL);
        }

        if (property_get(GPS_LONG_PROP, longitude, "0")) {
            ctx->location.longitude = strtod(longitude, NULL);
        }

        /// TODO: add support for change this
        ctx->location.accuracy = 1;

        ctx->location.timestamp = now_ms();
        ctx->cb->location_cb(&ctx->location);

        pthread_mutex_lock(&ctx->mutex);
        active = !ctx->stopped;
        pthread_mutex_unlock(&ctx->mutex);

        // Wait for half of a second
        tv.tv_usec = 50000;
        tv.tv_sec  = 0;
        select(0, NULL, NULL, NULL, &tv);
    }
}

// gps interface

static int
prop_gps_init(GpsCallbacks* callbacks)
{
    TRACE(__FUNCTION__);

    if (!_prop_gps_ctx->initialized) {
        _prop_gps_ctx->location.size = sizeof (GpsLocation);
        _prop_gps_ctx->cb = callbacks;
        pthread_mutex_init(&_prop_gps_ctx->mutex, NULL);
        _prop_gps_ctx->initialized = true;
    }

    return EXIT_SUCCESS;
}

static void
prop_gps_cleanup(void)
{
    TRACE(__FUNCTION__);
    pthread_mutex_destroy(&_prop_gps_ctx->mutex);
}


static int
prop_gps_start()
{
    TRACE(__FUNCTION__);

    if (_prop_gps_ctx->stopped) {
        _prop_gps_ctx->stopped = false;
        _prop_gps_ctx->thread = _prop_gps_ctx->cb->create_thread_cb("prop_gps_thread", prop_gps_thread_main, _prop_gps_ctx);
    }

    return EXIT_SUCCESS;
}


static int
prop_gps_stop()
{
    TRACE(__FUNCTION__);

    if (_prop_gps_ctx->initialized) {
        pthread_mutex_lock(&_prop_gps_ctx->mutex);
        _prop_gps_ctx->stopped = true;
        pthread_mutex_unlock(&_prop_gps_ctx->mutex);
        void*  dummy;
        pthread_join(_prop_gps_ctx->thread, &dummy);
    }

    return EXIT_SUCCESS;
}


static int
prop_gps_inject_time(GpsUtcTime time, int64_t timeReference, int uncertainty)
{
    TRACE(__FUNCTION__);
    return EXIT_SUCCESS;
}

static int
prop_gps_inject_location(double latitude, double longitude, float accuracy)
{
    TRACE(__FUNCTION__);
    return EXIT_SUCCESS;
}

static void
prop_gps_delete_aiding_data(GpsAidingData flags)
{
    TRACE(__FUNCTION__);
}

static int prop_gps_set_position_mode(GpsPositionMode mode, int fix_frequency)
{
    TRACE(__FUNCTION__);
    return EXIT_SUCCESS;
}

static const void*
prop_gps_get_extension(const char* name)
{
    TRACE("%s extension: %s", __FUNCTION__, name);
    // no extensions supported
    return NULL;
}

static const GpsInterface  propGpsInterface = {
    sizeof(GpsInterface),
    prop_gps_init,
    prop_gps_start,
    prop_gps_stop,
    prop_gps_cleanup,
    prop_gps_inject_time,
    prop_gps_inject_location,
    prop_gps_delete_aiding_data,
    prop_gps_set_position_mode,
    prop_gps_get_extension,
};

const GpsInterface* gps__get_gps_interface(struct gps_device_t* dev)
{
    TRACE(__FUNCTION__);
    return &propGpsInterface;
}

static int open_gps(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    TRACE(__FUNCTION__);
    struct gps_device_t *dev = malloc(sizeof(struct gps_device_t));
    memset(dev, 0, sizeof(struct gps_device_t));
    memset(_prop_gps_ctx, 0, sizeof(struct prop_gps_context));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = module;
    dev->get_gps_interface = gps__get_gps_interface;

    *device = (struct hw_device_t*)dev;
    return EXIT_SUCCESS;
}


static struct hw_module_methods_t gps_module_methods = {
    .open = open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "Properties based GPS Module",
    .author = "Dmitry Adzhiev",
    .methods = &gps_module_methods,
};
