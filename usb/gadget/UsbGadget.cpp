/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "android.hardware.usb.gadget.aidl-service"

#include "UsbGadget.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/properties.h>

#include <aidl/android/frameworks/stats/IStats.h>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {
namespace gadget {

using ::android::base::GetBoolProperty;
using ::android::hardware::google::pixel::usb::kUvcEnabled;

UsbGadget::UsbGadget() {
    if (access(OS_DESC_PATH, R_OK) != 0) {
        ALOGE("configfs setup not done yet");
        abort();
    }
}

void currentFunctionsAppliedCallback(bool functionsApplied, void *payload) {
    UsbGadget *gadget = (UsbGadget *)payload;
    gadget->mCurrentUsbFunctionsApplied = functionsApplied;
}

ScopedAStatus UsbGadget::getCurrentUsbFunctions(const shared_ptr<IUsbGadgetCallback> &callback,
        int64_t in_transactionId) {
    ScopedAStatus ret = callback->getCurrentUsbFunctionsCb(
        mCurrentUsbFunctions,
        mCurrentUsbFunctionsApplied ? Status::FUNCTIONS_APPLIED : Status::FUNCTIONS_NOT_APPLIED,
        in_transactionId);
    if (!ret.isOk())
        ALOGE("Call to getCurrentUsbFunctionsCb failed %s", ret.getDescription().c_str());

    return ScopedAStatus::ok();
}

ScopedAStatus UsbGadget::getUsbSpeed(const shared_ptr<IUsbGadgetCallback> &callback,
        int64_t in_transactionId) {
    std::string current_speed;
    if (ReadFileToString(SPEED_PATH, &current_speed)) {
        current_speed = Trim(current_speed);
        ALOGI("current USB speed is %s", current_speed.c_str());
        if (current_speed == "low-speed")
            mUsbSpeed = UsbSpeed::LOWSPEED;
        else if (current_speed == "full-speed")
            mUsbSpeed = UsbSpeed::FULLSPEED;
        else if (current_speed == "high-speed")
            mUsbSpeed = UsbSpeed::HIGHSPEED;
        else if (current_speed == "super-speed")
            mUsbSpeed = UsbSpeed::SUPERSPEED;
        else if (current_speed == "super-speed-plus")
            mUsbSpeed = UsbSpeed::SUPERSPEED_10Gb;
        else if (current_speed == "UNKNOWN")
            mUsbSpeed = UsbSpeed::UNKNOWN;
        else
            mUsbSpeed = UsbSpeed::UNKNOWN;
    } else {
        ALOGE("Fail to read current speed");
        mUsbSpeed = UsbSpeed::UNKNOWN;
    }

    if (callback) {
        ScopedAStatus ret = callback->getUsbSpeedCb(mUsbSpeed, in_transactionId);

        if (!ret.isOk())
            ALOGE("Call to getUsbSpeedCb failed %s", ret.getDescription().c_str());
    }

    return ScopedAStatus::ok();
}

Status UsbGadget::tearDownGadget() {
    if (Status(resetGadget()) != Status::SUCCESS){
        return Status::ERROR;
    }

    if (monitorFfs.isMonitorRunning()) {
        monitorFfs.reset();
    } else {
        ALOGI("mMonitor not running");
    }
    return Status::SUCCESS;
}

static Status validateAndSetVidPid(int64_t functions) {
    Status ret;
    std::string vendorFunctions = getVendorFunctions();

    switch (functions) {
        case GadgetFunction::MTP:
            if (vendorFunctions == "diag") {
                ret = Status(setVidPid("0x05C6", "0x901B"));
            } else {
                if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                    ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                    ret = Status::CONFIGURATION_NOT_SUPPORTED;
                } else {
                    ret = Status(setVidPid("0x18d1", "0x4ee1"));
                }
            }
            break;
        case GadgetFunction::ADB | GadgetFunction::MTP:
            if (vendorFunctions == "diag") {
                ret = Status(setVidPid("0x05C6", "0x903A"));
            } else {
                if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                    ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                    ret = Status::CONFIGURATION_NOT_SUPPORTED;
                } else {
                    ret = Status(setVidPid("0x18d1", "0x4ee2"));
                }
            }
            break;
        case GadgetFunction::RNDIS:
            if (vendorFunctions == "diag") {
                ret = Status(setVidPid("0x05C6", "0x902C"));
            } else if (vendorFunctions == "serial_cdev,diag") {
                ret = Status(setVidPid("0x05C6", "0x90B5"));
            } else if (vendorFunctions == "diag,diag_mdm,qdss,qdss_mdm,serial_cdev,dpl_gsi") {
                ret = Status(setVidPid("0x05C6", "0x90E6"));
            } else {
                if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                    ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                    ret = Status::CONFIGURATION_NOT_SUPPORTED;
                } else {
                    ret = Status(setVidPid("0x18d1", "0x4ee3"));
                }
            }
            break;
        case GadgetFunction::ADB | GadgetFunction::RNDIS:
            if (vendorFunctions == "diag") {
                ret = Status(setVidPid("0x05C6", "0x902D"));
            } else if (vendorFunctions == "serial_cdev,diag") {
                ret = Status(setVidPid("0x05C6", "0x90B6"));
            } else if (vendorFunctions == "diag,diag_mdm,qdss,qdss_mdm,serial_cdev,dpl_gsi") {
                ret = Status(setVidPid("0x05C6", "0x90E7"));
            } else {
                if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                    ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                    ret = Status::CONFIGURATION_NOT_SUPPORTED;
                } else {
                    ret = Status(setVidPid("0x18d1", "0x4ee4"));
                }
            }
            break;
        case GadgetFunction::PTP:
            if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else {
                ret = Status(setVidPid("0x18d1", "0x4ee5"));
            }
            break;
        case GadgetFunction::ADB |
                GadgetFunction::PTP:
            if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else {
                ret = Status(setVidPid("0x18d1", "0x4ee6"));
            }
            break;
        case GadgetFunction::ADB:
            if (vendorFunctions == "diag") {
                ret = Status(setVidPid("0x05C6", "0x901D"));
            } else if (vendorFunctions == "diag,serial_cdev,rmnet_gsi") {
                ret = Status(setVidPid("0x05C6", "0x9091"));
            } else if (vendorFunctions == "diag,serial_cdev") {
                ret = Status(setVidPid("0x05C6", "0x901F"));
            } else if (vendorFunctions == "diag,serial_cdev,rmnet_gsi,dpl_gsi,qdss") {
                ret = Status(setVidPid("0x05C6", "0x90DB"));
            } else if (vendorFunctions ==
                       "diag,diag_mdm,qdss,qdss_mdm,serial_cdev,dpl_gsi,rmnet_gsi") {
                ret = Status(setVidPid("0x05C6", "0x90E5"));
            } else {
                if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                    ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                    ret = Status::CONFIGURATION_NOT_SUPPORTED;
                } else {
                    ret = Status(setVidPid("0x18d1", "0x4ee7"));
                }
            }
            break;
        case GadgetFunction::MIDI:
            if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else {
                ret = Status(setVidPid("0x18d1", "0x4ee8"));
            }
            break;
        case GadgetFunction::ADB |
                GadgetFunction::MIDI:
            if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else {
                ret = Status(setVidPid("0x18d1", "0x4ee9"));
            }
            break;
        case GadgetFunction::ACCESSORY:
            if (!(vendorFunctions == "user" || vendorFunctions == ""))
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
            ret = Status(setVidPid("0x18d1", "0x2d00"));
            break;
        case GadgetFunction::ADB |
                 GadgetFunction::ACCESSORY:
            if (!(vendorFunctions == "user" || vendorFunctions == ""))
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
            ret = Status(setVidPid("0x18d1", "0x2d01"));
            break;
        case GadgetFunction::AUDIO_SOURCE:
            if (!(vendorFunctions == "user" || vendorFunctions == ""))
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
            ret = Status(setVidPid("0x18d1", "0x2d02"));
            break;
        case GadgetFunction::ADB |
                GadgetFunction::AUDIO_SOURCE:
            if (!(vendorFunctions == "user" || vendorFunctions == ""))
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
            ret = Status(setVidPid("0x18d1", "0x2d03"));
            break;
        case GadgetFunction::ACCESSORY |
                GadgetFunction::AUDIO_SOURCE:
            if (!(vendorFunctions == "user" || vendorFunctions == ""))
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
            ret = Status(setVidPid("0x18d1", "0x2d04"));
            break;
        case GadgetFunction::ADB |
                GadgetFunction::ACCESSORY |
                GadgetFunction::AUDIO_SOURCE:
            if (!(vendorFunctions == "user" || vendorFunctions == ""))
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
            ret = Status(setVidPid("0x18d1", "0x2d05"));
            break;
        case GadgetFunction::UVC:
            if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else if (!GetBoolProperty(kUvcEnabled, false)) {
                ALOGE("UVC function not enabled by config");
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else {
                ret = Status(setVidPid("0x18d1", "0x4eed"));
            }
            break;
        case GadgetFunction::ADB | GadgetFunction::UVC:
            if (!(vendorFunctions == "user" || vendorFunctions == "")) {
                ALOGE("Invalid vendorFunctions set: %s", vendorFunctions.c_str());
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else if (!GetBoolProperty(kUvcEnabled, false)) {
                ALOGE("UVC function not enabled by config");
                ret = Status::CONFIGURATION_NOT_SUPPORTED;
            } else {
                ret = Status(setVidPid("0x18d1", "0x4eee"));
            }
            break;
        default:
            ALOGE("Combination not supported");
            ret = Status::CONFIGURATION_NOT_SUPPORTED;
    }
    return ret;
}

ScopedAStatus UsbGadget::reset() {
    ALOGI("USB Gadget reset");

    if (!WriteStringToFile("none", PULLUP_PATH)) {
        ALOGI("Gadget cannot be pulled down");
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Gadget cannot be pulled down");
    }

    usleep(kDisconnectWaitUs);

    if (!WriteStringToFile(kGadgetName, PULLUP_PATH)) {
        ALOGI("Gadget cannot be pulled up");
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Gadget cannot be pulled up");
    }

    return ScopedAStatus::ok();
}

Status UsbGadget::setupFunctions(long functions,
        const shared_ptr<IUsbGadgetCallback> &callback, uint64_t timeout,
        int64_t in_transactionId) {
    bool ffsEnabled = false;
    int i = 0;

    if (Status(addGenericAndroidFunctions(&monitorFfs, functions, &ffsEnabled, &i)) !=
        Status::SUCCESS)
        return Status::ERROR;

    std::string vendorFunctions = getVendorFunctions();

    if (vendorFunctions != "") {
        ALOGI("enable usbradio debug functions");
        char *function = strtok(const_cast<char *>(vendorFunctions.c_str()), ",");
        while (function != NULL) {
            if (string(function) == "diag" && linkFunction("diag.diag", i++))
                return Status::ERROR;
            if (string(function) == "diag_mdm" && linkFunction("diag.diag_mdm", i++))
                return Status::ERROR;
            if (string(function) == "qdss" && linkFunction("qdss.qdss", i++))
                return Status::ERROR;
            if (string(function) == "qdss_mdm" && linkFunction("qdss.qdss_mdm", i++))
                return Status::ERROR;
            if (string(function) == "serial_cdev" && linkFunction("cser.dun.0", i++))
                return Status::ERROR;
            if (string(function) == "dpl_gsi" && linkFunction("gsi.dpl", i++))
                return Status::ERROR;
            if (string(function) == "rmnet_gsi" && linkFunction("gsi.rmnet", i++))
                return Status::ERROR;
            function = strtok(NULL, ",");
        }
    }

    if ((functions & GadgetFunction::ADB) != 0) {
        ffsEnabled = true;
        if (Status(addAdb(&monitorFfs, &i)) != Status::SUCCESS)
            return Status::ERROR;
    }

    // Pull up the gadget right away when there are no ffs functions.
    if (!ffsEnabled) {
        if (!WriteStringToFile(kGadgetName, PULLUP_PATH))
            return Status::ERROR;
        mCurrentUsbFunctionsApplied = true;
        if (callback)
            callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS, in_transactionId);
        return Status::SUCCESS;
    }

    monitorFfs.registerFunctionsAppliedCallback(&currentFunctionsAppliedCallback, this);
    // Monitors the ffs paths to pull up the gadget when descriptors are written.
    // Also takes of the pulling up the gadget again if the userspace process
    // dies and restarts.
    monitorFfs.startMonitor();

    if (kDebug)
        ALOGI("Mainthread in Cv");

    if (callback) {
        bool pullup = monitorFfs.waitForPullUp(timeout);
        ScopedAStatus ret = callback->setCurrentUsbFunctionsCb(
            functions, pullup ? Status::SUCCESS : Status::ERROR, in_transactionId);
        if (!ret.isOk()) {
            ALOGE("setCurrentUsbFunctionsCb error %s", ret.getDescription().c_str());
            return Status::ERROR;
        }
    }
    return Status::SUCCESS;
}

ScopedAStatus UsbGadget::setCurrentUsbFunctions(long functions,
                                               const shared_ptr<IUsbGadgetCallback> &callback,
                                               int64_t timeout,
                                               int64_t in_transactionId) {
    std::unique_lock<std::mutex> lk(mLockSetCurrentFunction);

    mCurrentUsbFunctions = functions;
    mCurrentUsbFunctionsApplied = false;

    // Unlink the gadget and stop the monitor if running.
    Status status = tearDownGadget();
    if (status != Status::SUCCESS) {
        goto error;
    }

    ALOGI("Returned from tearDown gadget");

    // Leave the gadget pulled down to give time for the host to sense disconnect.
    usleep(kDisconnectWaitUs);

    if (functions == GadgetFunction::NONE) {
        if (callback == NULL)
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "callback == NULL");
        ScopedAStatus ret = callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS, in_transactionId);
        if (!ret.isOk())
            ALOGE("Error while calling setCurrentUsbFunctionsCb %s", ret.getDescription().c_str());
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Error while calling setCurrentUsbFunctionsCb");
    }

    status = validateAndSetVidPid(functions);

    if (status != Status::SUCCESS) {
        goto error;
    }

    status = setupFunctions(functions, callback, timeout, in_transactionId);
    if (status != Status::SUCCESS) {
        goto error;
    }

    ALOGI("Usb Gadget setcurrent functions called successfully");
    return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Usb Gadget setcurrent functions called successfully");


error:
    ALOGI("Usb Gadget setcurrent functions failed");
    if (callback == NULL)
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Usb Gadget setcurrent functions failed");
    ScopedAStatus ret = callback->setCurrentUsbFunctionsCb(functions, status, in_transactionId);
    if (!ret.isOk())
        ALOGE("Error while calling setCurrentUsbFunctionsCb %s", ret.getDescription().c_str());
    return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Error while calling setCurrentUsbFunctionsCb");
}
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
}  // aidl
