/*
 * EEZ PSU Firmware
 * Copyright (C) 2016-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "psu.h"

#include "persist_conf.h"
#include "sound.h"
#include "calibration.h"

#include "gui_calibration.h"
#include "gui_keypad.h"
#include "gui_numeric_keypad.h"

namespace eez {
namespace psu {
namespace gui {
namespace calibration {

static void (*g_checkPasswordOkCallback)();
static void (*g_checkPasswordInvalidCallback)();

static char g_newPassword[PASSWORD_MAX_LENGTH];

Channel *g_channel;
int g_stepNum;

////////////////////////////////////////////////////////////////////////////////

void checkPasswordOkCallback(char *text) {
	int nPassword = strlen(persist_conf::dev_conf.calibration_password);
	int nText = strlen(text);
	if (nPassword == nText && strncmp(persist_conf::dev_conf.calibration_password, text, nText) == 0) {
		g_checkPasswordOkCallback();
	} else {
		// entered password doesn't match, 
		errorMessage(PSTR("Invalid password!"), g_checkPasswordInvalidCallback);
	}
}

void checkPassword(const char *label, void (*ok)(), void (*invalid)()) {
	g_checkPasswordOkCallback = ok;
	g_checkPasswordInvalidCallback = invalid;
	keypad::start(label, 0, PASSWORD_MAX_LENGTH, true, checkPasswordOkCallback, invalid);
}

////////////////////////////////////////////////////////////////////////////////

void onRetypeNewPasswordOk(char *text) {
	if (strncmp(g_newPassword, text, strlen(text)) != 0) {
		// retyped new password doesn't match
		errorMessage(PSTR("Password doesn't match!"), showPreviousPage);
		return;
	}
	
	if (!persist_conf::changePassword(g_newPassword, strlen(g_newPassword))) {
		// failed to save changed password
		errorMessage(PSTR("Failed to change password!"), showPreviousPage);
		return;
	}

	// success
	infoMessage(PSTR("Password changed!"), showPreviousPage);
}

void onNewPasswordOk(char *text) {
	int textLength = strlen(text);

	int16_t err;
	if (!persist_conf::isPasswordValid(text, textLength, err)) {
		// invalid password (probably too short), return to keypad
		errorMessage(PSTR("Password too short!"));
		return;
	}

	strcpy(g_newPassword, text);
	keypad::start(PSTR("Retype new password: "), 0, PASSWORD_MAX_LENGTH, true, onRetypeNewPasswordOk, showPreviousPage);
}

void onOldPasswordOk() {
	keypad::start(PSTR("New password: "), 0, PASSWORD_MAX_LENGTH, true, onNewPasswordOk, showPreviousPage);
}

void editPassword() {
	checkPassword(PSTR("Current password: "), onOldPasswordOk, showPreviousPage);
}

////////////////////////////////////////////////////////////////////////////////

void selectChannel() {
	g_channel = &Channel::get(found_widget_at_down.cursor.iChannel);
}

void onStartPasswordOk() {
	psu::calibration::start(g_channel);
	g_stepNum = 0;
	showPage(PAGE_ID_SYS_SETTINGS_CAL_CH_WIZ_STEP, false);
}

void start() {
	checkPassword(PSTR("Password: "), onStartPasswordOk, showPreviousPage);
}

data::Value getData(const data::Cursor &cursor, uint8_t id) {
	if (id == DATA_ID_CALIBRATION_CHANNEL_LABEL) {
		Channel &channel = cursor.iChannel == -1 ? *g_channel : Channel::get(cursor.iChannel);
		return data::Value(channel.index, data::VALUE_TYPE_CHANNEL_LABEL);
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_STATUS) {
		Channel &channel = cursor.iChannel == -1 ? *g_channel : Channel::get(cursor.iChannel);
		return data::Value(channel.isCalibrationExists() ? 1 : 0);
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_DATE) {
		Channel &channel = cursor.iChannel == -1 ? *g_channel : Channel::get(cursor.iChannel);
		return data::Value(channel.cal_conf.calibration_date);
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_REMARK) {
		Channel &channel = cursor.iChannel == -1 ? *g_channel : Channel::get(cursor.iChannel);
		return data::Value(channel.cal_conf.calibration_remark);
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_STEP_NUM) {
		return data::Value(g_stepNum);
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_STEP_VALUE) {
		switch (g_stepNum) {
		case 0: return data::Value(psu::calibration::voltage.min, data::VALUE_TYPE_FLOAT_VOLT);
		case 1: return data::Value(psu::calibration::voltage.mid, data::VALUE_TYPE_FLOAT_VOLT);
		case 2: return data::Value(psu::calibration::voltage.max, data::VALUE_TYPE_FLOAT_VOLT);
		case 3: return data::Value(psu::calibration::current.min, data::VALUE_TYPE_FLOAT_AMPER);
		case 4: return data::Value(psu::calibration::current.mid, data::VALUE_TYPE_FLOAT_AMPER);
		case 5: return data::Value(psu::calibration::current.max, data::VALUE_TYPE_FLOAT_AMPER);
		case 6: return data::Value(psu::calibration::getRemark());
		}
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_STEP_STATUS) {
		switch (g_stepNum) {
		case 0: return data::Value(psu::calibration::voltage.min_set ? 1 : 0);
		case 1: return data::Value(psu::calibration::voltage.mid_set ? 1 : 0);
		case 2: return data::Value(psu::calibration::voltage.max_set ? 1 : 0);
		case 3: return data::Value(psu::calibration::current.min_set ? 1 : 0);
		case 4: return data::Value(psu::calibration::current.mid_set ? 1 : 0);
		case 5: return data::Value(psu::calibration::current.max_set ? 1 : 0);
		case 6: return data::Value(psu::calibration::isRemarkSet() ? 1 : 0);
		}
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_STEP_PREV_ENABLED) {
		return data::Value(g_stepNum > 0 ? 1 : 0);
	} else if (id == DATA_ID_CALIBRATION_CHANNEL_STEP_NEXT_ENABLED) {
		return data::Value(g_stepNum < MAX_STEP_NUM ? 1 : 0);
	}

	return data::Value();
}

psu::calibration::Value *getCalibrationValue() {
	if (g_stepNum < 3) {
		return &psu::calibration::voltage;
	}
	return &psu::calibration::current;
}

void onSetOk(float value) {
	psu::calibration::Value *calibrationValue = getCalibrationValue();

    float adc = calibrationValue->getAdcValue();
	if (calibrationValue->checkRange(value, adc)) {
		calibrationValue->setData(value, adc);

		showPreviousPage();
		nextStep();
	} else {
		errorMessage(PSTR("Value out of range!"));
	}
}

void onSetRemarkOk(char *remark) {
	psu::calibration::setRemark(remark, strlen(remark));
	showPreviousPage();
	if (g_stepNum < 6) {
		nextStep();
	} else {
		int16_t scpiErr;
		if (psu::calibration::canSave(scpiErr)) {
			nextStep();
		}
	}
}

void set() {
	if (g_stepNum < 6) {
		switch (g_stepNum) {
		case 0: psu::calibration::voltage.setLevel(psu::calibration::LEVEL_MIN); break;
		case 1: psu::calibration::voltage.setLevel(psu::calibration::LEVEL_MID); break;
		case 2: psu::calibration::voltage.setLevel(psu::calibration::LEVEL_MAX); break;
		case 3: psu::calibration::current.setLevel(psu::calibration::LEVEL_MIN); break;
		case 4: psu::calibration::current.setLevel(psu::calibration::LEVEL_MID); break;
		case 5: psu::calibration::current.setLevel(psu::calibration::LEVEL_MAX); break;
		}
	
		psu::calibration::Value *calibrationValue = getCalibrationValue();

		if (calibrationValue == &psu::calibration::voltage) {
			numeric_keypad::start(0, data::VALUE_TYPE_FLOAT_VOLT, g_channel->U_MIN, g_channel->U_MAX, onSetOk, showPreviousPage);
		} else {
			numeric_keypad::start(0, data::VALUE_TYPE_FLOAT_AMPER, g_channel->I_MIN, g_channel->I_MAX, onSetOk, showPreviousPage);
		}
	} else if (g_stepNum == 6) {
		keypad::start(0, psu::calibration::isRemarkSet() ? psu::calibration::getRemark() : 0, 32, false, onSetRemarkOk, showPreviousPage);
	}
}

void previousStep() {
	if (g_stepNum > 0) {
		--g_stepNum;
		showPage(PAGE_ID_SYS_SETTINGS_CAL_CH_WIZ_STEP, false);
	}
}

void nextStep() {
	if (g_stepNum == 6) {
		int16_t scpiErr;
		if (!psu::calibration::canSave(scpiErr)) {
			if (scpiErr == SCPI_ERROR_INVALID_CAL_DATA) {
				errorMessage(PSTR("Invalid calibration data!"));
			} else {
				errorMessage(PSTR("Missing calibration data!"));
			}
			return;
		}
	}

	++g_stepNum;

	if (g_stepNum < 7) {
		showPage(PAGE_ID_SYS_SETTINGS_CAL_CH_WIZ_STEP, false);
	} else {
		showPage(PAGE_ID_SYS_SETTINGS_CAL_CH_WIZ_FINISH, false);
	}
}

void save() {
	if (psu::calibration::save()) {
		errorMessage(PSTR("Calibration data saved!"), showPreviousPage);
	} else {
		errorMessage(PSTR("Save failed!"));
	}
}

void stop() {
	psu::calibration::stop();
}

}
}
}
} // namespace eez::psu::gui::calibration