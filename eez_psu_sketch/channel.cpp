/*
 * EEZ PSU Firmware
 * Copyright (C) 2015 Envox d.o.o.
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
#include "serial_psu.h"
#include "ethernet.h"
#include "bp.h"
#include "board.h"
#include "ioexp.h"
#include "adc.h"
#include "dac.h"
#include "bp.h"
#include "calibration.h"
#include "persist_conf.h"
#include "sound.h"
#include "profile.h"

namespace eez {
namespace psu {

using namespace scpi;

////////////////////////////////////////////////////////////////////////////////

#define CHANNEL(INDEX, PINS, PARAMS) Channel(INDEX, PINS, PARAMS)
Channel channels[CH_MAX] = { CHANNELS };

////////////////////////////////////////////////////////////////////////////////

Channel &Channel::get(int channel_index) {
    return channels[channel_index];
}

////////////////////////////////////////////////////////////////////////////////

void Channel::Value::init(float def_step) {
    set = 0;
    mon_dac = 0;
    mon = 0;
    step = def_step;
}

////////////////////////////////////////////////////////////////////////////////

#ifdef EEZ_PSU_SIMULATOR

void Channel::Simulator::setLoadEnabled(bool value) {
    load_enabled = value;
}

bool Channel::Simulator::getLoadEnabled() {
    return load_enabled;
}

void Channel::Simulator::setLoad(float value) {
    load = value;
    profile::save();
}

float Channel::Simulator::getLoad() {
    return load;
}

#endif

////////////////////////////////////////////////////////////////////////////////

Channel::Channel(
    int8_t index_,
    uint8_t isolator_pin_, uint8_t ioexp_pin_, uint8_t convend_pin_, uint8_t adc_pin_, uint8_t dac_pin_,
    uint16_t bp_led_out_plus_, uint16_t bp_led_out_minus_, uint16_t bp_led_sense_plus_, uint16_t bp_led_sense_minus_, uint16_t bp_relay_sense_,
    uint8_t cc_led_pin_, uint8_t cv_led_pin_,
    float U_MIN_, float U_DEF_, float U_MAX_, float U_MIN_STEP_, float U_DEF_STEP_, float U_MAX_STEP_, float U_CAL_VAL_MIN_, float U_CAL_VAL_MID_, float U_CAL_VAL_MAX_, float U_CURR_CAL_,
    bool OVP_DEFAULT_STATE_, float OVP_MIN_DELAY_, float OVP_DEFAULT_DELAY_, float OVP_MAX_DELAY_,
    float I_MIN_, float I_DEF_, float I_MAX_, float I_MIN_STEP_, float I_DEF_STEP_, float I_MAX_STEP_, float I_CAL_VAL_MIN_, float I_CAL_VAL_MID_, float I_CAL_VAL_MAX_, float I_VOLT_CAL_,
    bool OCP_DEFAULT_STATE_, float OCP_MIN_DELAY_, float OCP_DEFAULT_DELAY_, float OCP_MAX_DELAY_,
    bool OPP_DEFAULT_STATE_, float OPP_MIN_DELAY_, float OPP_DEFAULT_DELAY_, float OPP_MAX_DELAY_, float OPP_MIN_LEVEL_, float OPP_DEFAULT_LEVEL_, float OPP_MAX_LEVEL_
    )
    :
    index(index_),
    isolator_pin(isolator_pin_), ioexp_pin(ioexp_pin_), convend_pin(convend_pin_), adc_pin(adc_pin_), dac_pin(dac_pin_),
    bp_led_out_plus(bp_led_out_plus_), bp_led_out_minus(bp_led_out_minus_), bp_led_sense_plus(bp_led_sense_plus_), bp_led_sense_minus(bp_led_sense_minus_), bp_relay_sense(bp_relay_sense_),
    cc_led_pin(cc_led_pin_), cv_led_pin(cv_led_pin_),
    U_MIN(U_MIN_), U_DEF(U_DEF_), U_MAX(U_MAX_), U_MIN_STEP(U_MIN_STEP_), U_DEF_STEP(U_DEF_STEP_), U_MAX_STEP(U_MAX_STEP_), U_CAL_VAL_MIN(U_CAL_VAL_MIN_), U_CAL_VAL_MID(U_CAL_VAL_MID_), U_CAL_VAL_MAX(U_CAL_VAL_MAX_), U_CURR_CAL(U_CURR_CAL_),
    OVP_DEFAULT_STATE(OVP_DEFAULT_STATE_), OVP_MIN_DELAY(OVP_MIN_DELAY_), OVP_DEFAULT_DELAY(OVP_DEFAULT_DELAY_), OVP_MAX_DELAY(OVP_MAX_DELAY_),
    I_MIN(I_MIN_), I_DEF(I_DEF_), I_MAX(I_MAX_), I_MIN_STEP(I_MIN_STEP_), I_DEF_STEP(I_DEF_STEP_), I_MAX_STEP(I_MAX_STEP_), I_CAL_VAL_MIN(I_CAL_VAL_MIN_), I_CAL_VAL_MID(I_CAL_VAL_MID_), I_CAL_VAL_MAX(I_CAL_VAL_MAX_), I_VOLT_CAL(I_VOLT_CAL_),
    OCP_DEFAULT_STATE(OCP_DEFAULT_STATE_), OCP_MIN_DELAY(OCP_MIN_DELAY_), OCP_DEFAULT_DELAY(OCP_DEFAULT_DELAY_), OCP_MAX_DELAY(OCP_MAX_DELAY_),
    OPP_DEFAULT_STATE(OPP_DEFAULT_STATE_), OPP_MIN_DELAY(OPP_MIN_DELAY_), OPP_DEFAULT_DELAY(OPP_DEFAULT_DELAY_), OPP_MAX_DELAY(OPP_MAX_DELAY_), OPP_MIN_LEVEL(OPP_MIN_LEVEL_), OPP_DEFAULT_LEVEL(OPP_DEFAULT_LEVEL_), OPP_MAX_LEVEL(OPP_MAX_LEVEL_),
    ioexp(*this),
    adc(*this),
    dac(*this)
{
}

void Channel::protectionEnter(ProtectionValue &cpv) {
    outputEnable(false);

    cpv.flags.tripped = 1;

    int bit_mask = reg_get_ques_isum_bit_mask_for_channel_protection_value(this, cpv);
    setQuesBits(bit_mask, true);

    sound::playBeep();
}

void Channel::protectionCheck(ProtectionValue &cpv) {
    bool state;
    bool condition;
    float delay;

    if (IS_OVP_VALUE(this, cpv)) {
        state = prot_conf.flags.u_state;
        condition = flags.cv_mode && (!flags.cc_mode || fabs(i.mon - i.set) >= 0.01);
        delay = prot_conf.u_delay;
        delay -= PROT_DELAY_CORRECTION;
    }
    else if (IS_OCP_VALUE(this, cpv)) {
        state = prot_conf.flags.i_state;
        condition = flags.cc_mode && (!flags.cv_mode || fabs(u.mon - u.set) >= 0.01);
        delay = prot_conf.i_delay;
        delay -= PROT_DELAY_CORRECTION;
    }
    else {
        state = prot_conf.flags.p_state;
        condition = u.mon * i.mon > prot_conf.p_level;
        delay = prot_conf.p_delay;
    }

    if (state && isOutputEnabled() && condition) {
        if (delay > 0) {
            if (cpv.flags.alarmed) {
                if (micros() - cpv.alarm_started >= delay * 1000000UL) {
                    cpv.flags.alarmed = 0;

                    if (IS_OVP_VALUE(this, cpv)) {
                        DebugTrace("OVP condition: CV_MODE=%d, CC_MODE=%d, I DIFF=%d mA", (int)flags.cv_mode, (int)flags.cc_mode, (int)(fabs(i.mon - i.set) * 1000));
                    }
                    else if (IS_OCP_VALUE(this, cpv)) {
                        DebugTrace("OCP condition: CC_MODE=%d, CV_MODE=%d, U DIFF=%d mV", (int)flags.cc_mode, (int)flags.cv_mode, (int)(fabs(u.mon - u.set) * 1000));
                    }

                    protectionEnter(cpv);
                }
            }
            else {
                cpv.flags.alarmed = 1;
                cpv.alarm_started = micros();
            }
        }
        else {
            protectionEnter(cpv);
        }
    }
    else {
        cpv.flags.alarmed = 0;
    }
}

////////////////////////////////////////////////////////////////////////////////

bool Channel::init() {
    bool result = true;

    bool last_save_enabled = profile::enableSave(false);

    result &= ioexp.init();
    result &= adc.init();
    result &= dac.init();

    profile::enableSave(last_save_enabled);

    return result;
}

void Channel::onPowerDown() {
    bool last_save_enabled = profile::enableSave(false);

    outputEnable(false);
    remoteSensingEnable(false);

    profile::enableSave(last_save_enabled);
}

void Channel::reset() {
    flags.output_enabled = 0;
    flags.sense_enabled = 0;

    flags.cv_mode = 0;
    flags.cc_mode = 0;

    flags.power_ok = 0;

    ovp.flags.tripped = 0;
    ovp.flags.alarmed = 0;

    ocp.flags.tripped = 0;
    ocp.flags.alarmed = 0;

    opp.flags.tripped = 0;
    opp.flags.alarmed = 0;

    // CAL:STAT ON if valid calibrating data for both voltage and current exists in the nonvolatile memory, otherwise OFF.
    flags.cal_enabled = isCalibrationExists();

    // OUTP:PROT:CLE OFF
    // [SOUR[n]]:VOLT:PROT:TRIP? 0
    // [SOUR[n]]:CURR:PROT:TRIP? 0
    // [SOUR[n]]:POW:PROT:TRIP? 0
    clearProtection();

    // OUTP:SENS OFF
    remoteSensingEnable(false);

    // [SOUR[n]]:VOLT:PROT:DEL 
    // [SOUR[n]]:VOLT:PROT:STAT
    // [SOUR[n]]:CURR:PROT:DEL
    // [SOUR[n]]:CURR:PROT:STAT 
    // [SOUR[n]]:POW:PROT[:LEV]
    // [SOUR[n]]:POW:PROT:DEL
    // [SOUR[n]]:POW:PROT:STAT -> set all to default
    clearProtectionConf();

    // [SOUR[n]]:CURR
    // [SOUR[n]]:CURR:STEP
    // [SOUR[n]]:VOLT
    // [SOUR[n]]:VOLT:STEP -> set all to default
    u.init(U_DEF_STEP);
    i.init(I_DEF_STEP);
}

void Channel::clearCalibrationConf() {
    cal_conf.flags.u_cal_params_exists = 0;
    cal_conf.flags.i_cal_params_exists = 0;

    cal_conf.u.min.dac = cal_conf.u.min.val = cal_conf.u.min.adc = U_CAL_VAL_MIN;
    cal_conf.u.mid.dac = cal_conf.u.mid.val = cal_conf.u.mid.adc = (U_CAL_VAL_MIN + U_CAL_VAL_MAX) / 2;
    cal_conf.u.max.dac = cal_conf.u.max.val = cal_conf.u.max.adc = U_CAL_VAL_MAX;

    cal_conf.i.min.dac = cal_conf.i.min.val = cal_conf.i.min.adc = I_CAL_VAL_MIN;
    cal_conf.i.mid.dac = cal_conf.i.mid.val = cal_conf.i.mid.adc = (I_CAL_VAL_MIN + I_CAL_VAL_MAX) / 2;
    cal_conf.i.max.dac = cal_conf.i.max.val = cal_conf.i.max.adc = I_CAL_VAL_MAX;

    strcpy(cal_conf.calibration_date, "");
    strcpy(cal_conf.calibration_remark, CALIBRATION_REMARK_INIT);
}

void Channel::clearProtectionConf() {
    prot_conf.flags.u_state = OVP_DEFAULT_STATE;
    prot_conf.flags.i_state = OCP_DEFAULT_STATE;
    prot_conf.flags.p_state = OPP_DEFAULT_STATE;

    prot_conf.u_delay = OVP_DEFAULT_DELAY;
    prot_conf.i_delay = OCP_DEFAULT_DELAY;
    prot_conf.p_delay = OPP_DEFAULT_DELAY;
    prot_conf.p_level = OPP_DEFAULT_LEVEL;
}

bool Channel::test() {
    bool last_save_enabled = profile::enableSave(false);

    outputEnable(false);
    remoteSensingEnable(false);

    ioexp.test();
    adc.test();
    dac.test();

    if (isOk()) {
        setVoltage(U_DEF);
        setCurrent(I_DEF);
    }

    profile::enableSave(last_save_enabled);
    profile::save();

    return isOk();
}

bool Channel::isPowerOk() {
    return flags.power_ok;
}

bool Channel::isTestFailed() {
    return ioexp.test_result == psu::TEST_FAILED ||
        adc.test_result == psu::TEST_FAILED ||
        dac.test_result == psu::TEST_FAILED;
}

bool Channel::isTestOk() {
    return ioexp.test_result == psu::TEST_OK &&
        adc.test_result == psu::TEST_OK &&
        dac.test_result == psu::TEST_OK;
}

bool Channel::isOk() {
    return psu::isPowerUp() && isPowerOk() && isTestOk();
}

void Channel::tick(unsigned long tick_usec) {
    ioexp.tick(tick_usec);
    adc.tick(tick_usec);

    // turn off DP after delay
    if (delayed_dp_off && tick_usec - delayed_dp_off_start >= DP_OFF_DELAY_PERIOD * 1000000L) {
        delayed_dp_off = false;
        doDpEnable(false);
    }
}

float Channel::remapAdcDataToVoltage(int16_t adc_data) {
    return util::remap((float)adc_data, (float)AnalogDigitalConverter::ADC_MIN, U_MIN, (float)AnalogDigitalConverter::ADC_MAX, U_MAX);
}

float Channel::remapAdcDataToCurrent(int16_t adc_data) {
    return util::remap((float)adc_data, (float)AnalogDigitalConverter::ADC_MIN, I_MIN, (float)AnalogDigitalConverter::ADC_MAX, I_MAX);
}

int16_t Channel::remapVoltageToAdcData(float value) {
    float adc_value = util::remap(value, U_MIN, (float)AnalogDigitalConverter::ADC_MIN, U_MAX, (float)AnalogDigitalConverter::ADC_MAX);
    return (int16_t)util::clamp(adc_value, (float)(-AnalogDigitalConverter::ADC_MAX - 1), (float)AnalogDigitalConverter::ADC_MAX);
}

int16_t Channel::remapCurrentToAdcData(float value) {
    float adc_value = util::remap(value, I_MIN, (float)AnalogDigitalConverter::ADC_MIN, I_MAX, (float)AnalogDigitalConverter::ADC_MAX);
    return (int16_t)util::clamp(adc_value, (float)(-AnalogDigitalConverter::ADC_MAX - 1), (float)AnalogDigitalConverter::ADC_MAX);
}

float Channel::readingToCalibratedValue(Value *cv, float mon_reading) {
    if (flags.cal_enabled) {
        if (cv == &u) {
            mon_reading = util::remap(mon_reading, cal_conf.u.min.adc, cal_conf.u.min.val, cal_conf.u.max.adc, cal_conf.u.max.val);
        }
        else {
            mon_reading = util::remap(mon_reading, cal_conf.i.min.adc, cal_conf.i.min.val, cal_conf.i.max.adc, cal_conf.i.max.val);
        }
    }
    return mon_reading;
}

void Channel::valueAddReading(Value *cv, float value) {
    cv->mon = readingToCalibratedValue(cv, value);
    protectionCheck(opp);
}

void Channel::valueAddReadingDac(Value *cv, float value) {
    cv->mon_dac = readingToCalibratedValue(cv, value);
}

#if CONF_DEBUG
extern int16_t debug::u_mon[CH_MAX];
extern int16_t debug::u_mon_dac[CH_MAX];
extern int16_t debug::i_mon[CH_MAX];
extern int16_t debug::i_mon_dac[CH_MAX];
#endif

void Channel::adcDataIsReady(int16_t data) {
    switch (adc.start_reg0) {
    case AnalogDigitalConverter::ADC_REG0_READ_U_MON:
#if CONF_DEBUG
        debug::u_mon[index - 1] = data;
#endif
        valueAddReading(&u, remapAdcDataToVoltage(data));
        adc.start(AnalogDigitalConverter::ADC_REG0_READ_I_MON);
        break;

    case AnalogDigitalConverter::ADC_REG0_READ_I_MON:
#if CONF_DEBUG
        debug::i_mon[index - 1] = data;
#endif
        valueAddReading(&i, remapAdcDataToCurrent(data));
        if (isOutputEnabled()) {
            adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_MON);
        }
        else {
            u.mon = 0;
            i.mon = 0;
            adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_SET);
        }
        break;

    case AnalogDigitalConverter::ADC_REG0_READ_U_SET:
#if CONF_DEBUG
        debug::u_mon_dac[index - 1] = data;
#endif
        valueAddReadingDac(&u, remapAdcDataToVoltage(data));
        adc.start(AnalogDigitalConverter::ADC_REG0_READ_I_SET);
        break;

    case AnalogDigitalConverter::ADC_REG0_READ_I_SET:
#if CONF_DEBUG
        debug::i_mon_dac[index - 1] = data;
#endif
        valueAddReadingDac(&i, remapAdcDataToCurrent(data));
        if (isOutputEnabled()) {
            adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_MON);
        }
        break;
    }
}

void Channel::updateBoardCcAndCvSwitch() {
    board::cvLedSwitch(this, isCvMode());
    board::ccLedSwitch(this, isCcMode());
}

void Channel::setCcMode(bool cc_mode) {
    if (!isOutputEnabled()) {
        cc_mode = 0;
    }

    if (cc_mode != flags.cc_mode) {
        flags.cc_mode = cc_mode;

        updateBoardCcAndCvSwitch();

        setOperBits(OPER_ISUM_CC, cc_mode);
        setQuesBits(QUES_ISUM_VOLT, cc_mode);
    }

    protectionCheck(ocp);
}

void Channel::setCvMode(bool cv_mode) {
    if (!isOutputEnabled()) {
        cv_mode = 0;
    }

    if (cv_mode != flags.cv_mode) {
        flags.cv_mode = cv_mode;

        updateBoardCcAndCvSwitch();

        setOperBits(OPER_ISUM_CV, cv_mode);
        setQuesBits(QUES_ISUM_CURR, cv_mode);
    }

    protectionCheck(ovp);
}

void Channel::event(uint8_t gpio, int16_t adc_data) {
    if (!psu::isPowerUp()) return;

    if (!(gpio & (1 << IOExpander::IO_BIT_IN_PWRGOOD))) {
        DebugTrace("Ch%d PWRGOOD bit changed to 0", index);
        flags.power_ok = 0;
        psu::generateError(SCPI_ERROR_CHANNEL_FAULT_DETECTED);
        psu::powerDownBySensor();
        return;
    }

    adcDataIsReady(adc_data);

    setCvMode(gpio & (1 << IOExpander::IO_BIT_IN_CV_ACTIVE) ? true : false);
    setCcMode(gpio & (1 << IOExpander::IO_BIT_IN_CC_ACTIVE) ? true : false);
}

void Channel::adcReadMonDac() {
    adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_SET);
    delay(ADC_TIMEOUT_MS * 2);
}

void Channel::adcReadAll() {
    if (isOutputEnabled()) {
        adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_SET);
        delay(ADC_TIMEOUT_MS * 3);
    }
    else {
        adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_MON);
        delay(ADC_TIMEOUT_MS * 4);
    }
}

void Channel::doDpEnable(bool enable) {
    // DP bit is active low
    ioexp.change_bit(IOExpander::IO_BIT_OUT_DP_ENABLE, !enable);
    setOperBits(OPER_ISUM_DP_OFF, !enable);
}

void Channel::updateOutputEnable() {
   ioexp.change_bit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE, flags.output_enabled);
}

void Channel::doOutputEnable(bool enable) {
    if (enable && !isOk()) {
        return;
    }

    flags.output_enabled = enable;

    ioexp.change_bit(IOExpander::IO_BIT_OUT_OUTPUT_ENABLE, enable);

    bp::switchOutput(this, enable);

    if (enable) {
        adc.start(AnalogDigitalConverter::ADC_REG0_READ_U_MON);
    }
    else {
        setCvMode(false);
        setCcMode(false);

        if (calibration::isEnabled()) {
            calibration::stop();
        }
    }

    if (enable) {
        delayed_dp_off = false;
        doDpEnable(true);
    }
    else {
        delayed_dp_off = true;
        delayed_dp_off_start = micros();
    }

    setOperBits(OPER_ISUM_OE_OFF, !enable);
}

void Channel::doRemoteSensingEnable(bool enable) {
    if (enable && !isOk()) {
        return;
    }
    flags.sense_enabled = enable;
    bp::switchSense(this, enable);
    setOperBits(OPER_ISUM_RSENS_ON, enable);
}


void Channel::update() {
    bool last_save_enabled = profile::enableSave(false);

    setVoltage(u.set);
    setCurrent(i.set);
    doOutputEnable(flags.output_enabled);
    doRemoteSensingEnable(flags.sense_enabled);

    profile::enableSave(last_save_enabled);
}

void Channel::outputEnable(bool enable) {
    if (enable != flags.output_enabled) {
        doOutputEnable(enable);
        profile::save();
    }
}

bool Channel::isOutputEnabled() {
    return psu::isPowerUp() && flags.output_enabled;
}

void Channel::remoteSensingEnable(bool enable) {
    if (enable != flags.sense_enabled) {
        doRemoteSensingEnable(enable);
        profile::save();
    }
}

bool Channel::isRemoteSensingEnabled() {
    return flags.sense_enabled;
}

void Channel::setVoltage(float value) {
    u.set = value;
    u.mon_dac = 0;

    if (flags.cal_enabled) {
        value = util::remap(value, cal_conf.u.min.val, cal_conf.u.min.dac, cal_conf.u.max.val, cal_conf.u.max.dac);
    }
    dac.set_voltage(value);

    profile::save();
}

void Channel::setCurrent(float value) {
    i.set = value;
    i.mon_dac = 0;

    if (flags.cal_enabled) {
        value = util::remap(value, cal_conf.i.min.val, cal_conf.i.min.dac, cal_conf.i.max.val, cal_conf.i.max.dac);
    }
    dac.set_current(value);

    profile::save();
}

bool Channel::isCalibrationExists() {
    return cal_conf.flags.i_cal_params_exists && cal_conf.flags.u_cal_params_exists;
}

bool Channel::isTripped() {
    return ovp.flags.tripped ||
        ocp.flags.tripped ||
        opp.flags.tripped ||
        temperature::isChannelTripped(this);
}

void Channel::clearProtection() {
    ovp.flags.tripped = 0;
    ovp.flags.alarmed = 0;
    setQuesBits(QUES_ISUM_OVP, false);

    ocp.flags.tripped = 0;
    ocp.flags.alarmed = 0;
    setQuesBits(QUES_ISUM_OCP, false);

    opp.flags.tripped = 0;
    opp.flags.alarmed = 0;
    setQuesBits(QUES_ISUM_OPP, false);
}

void Channel::setQuesBits(int bit_mask, bool on) {
    reg_set_ques_isum_bit(&serial::scpi_context, this, bit_mask, on);
    if (ethernet::test_result == psu::TEST_OK)
        reg_set_ques_isum_bit(&ethernet::scpi_context, this, bit_mask, on);
}

void Channel::setOperBits(int bit_mask, bool on) {
    reg_set_oper_isum_bit(&serial::scpi_context, this, bit_mask, on);
    if (ethernet::test_result == psu::TEST_OK)
        reg_set_oper_isum_bit(&ethernet::scpi_context, this, bit_mask, on);
}

}
} // namespace eez::psu