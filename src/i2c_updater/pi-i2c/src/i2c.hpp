/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. 
 */

#ifndef _I2C_HPP_
#define _I2C_HPP_

#include <string>
#include <vector>
#include <cstdint>

class tI2c {
public:
	tI2c(const std::string &Port);
	~tI2c(); // Destructor to close the file descriptor
	void write(uint8_t ubAddr, const std::vector<uint8_t> &vData);
	void read(uint8_t ubAddr, uint8_t *pDest, uint32_t ulReadSize);
	template <typename t_tContainer>
	void read(uint8_t ubAddr, t_tContainer &Cont) {
		read(ubAddr, Cont.data(), Cont.size());
	}

private:
	int m_I2cHandle;
	uint8_t m_currentSlaveAddr; // Cache the current slave address to avoid redundant ioctl calls
	void setSlaveAddress(uint8_t ubAddr);
};

#endif // _I2C_HPP_
