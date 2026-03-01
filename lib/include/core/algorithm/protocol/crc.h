/****************************************************************************
 *  Copyright (C) 2018 RoboMaster.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of?
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.? See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 ***************************************************************************/
/** @file protocol.h
 *  @version 1.0
 *  @date Oct 2017
 *
 *  @brief the communication protocol of main control with
 *         judge system and computer
 *
 *  @copyright 2017 DJI RoboMaster. All rights reserved.
 *
 */

#ifndef __CRC_H__
#define CRC_H

#include "stdint.h"

#define CRC8_SZ 1
#define CRC16_SZ 2

uint8_t verify_crc8_check_sum(uint8_t* pch_message, uint16_t dw_length);
uint8_t verify_crc16_check_sum(uint8_t* pch_message, uint32_t dw_length);

void append_crc8_check_sum(uint8_t* pch_message, uint16_t dw_length);
void append_crc16_check_sum(uint8_t* pch_message, uint32_t dw_length);

#endif
