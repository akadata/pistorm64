/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "i2c.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>

tI2c::tI2c(const std::string &Port)
	: m_I2cHandle(-1), m_currentSlaveAddr(0xFF) // Initialize with invalid address
{
	// Open the I2C bus file handle
	m_I2cHandle = open(Port.c_str(), O_RDWR);
	if(m_I2cHandle < 0) {
		throw std::runtime_error("i2c: can't open '" + Port + "': " + std::string(strerror(errno)));
	}
}

void tI2c::setSlaveAddress(uint8_t ubAddr) {
	if (m_currentSlaveAddr != ubAddr) {
		if(ioctl(m_I2cHandle, I2C_SLAVE, ubAddr) < 0) {
			throw std::runtime_error("i2c: ioctl(I2C_SLAVE, 0x" + std::to_string(ubAddr) +
									") failed: " + std::string(strerror(errno)));
		}
		m_currentSlaveAddr = ubAddr;
	}
}

void tI2c::write(uint8_t ubAddr, const std::vector<uint8_t> &vData) {
	setSlaveAddress(ubAddr);

	ssize_t BytesWritten = ::write(m_I2cHandle, vData.data(), vData.size());
	if(BytesWritten < 0) {
		throw std::runtime_error("i2c: write to 0x" + std::to_string(ubAddr) +
								" failed: " + std::string(strerror(errno)));
	}
	if(static_cast<size_t>(BytesWritten) != vData.size()) {
		throw std::runtime_error("i2c: short write to 0x" + std::to_string(ubAddr) +
								" wrote " + std::to_string(BytesWritten) +
								" expected " + std::to_string(vData.size()));
	}
}

void tI2c::read(uint8_t ubAddr, uint8_t *pDest, uint32_t ulReadSize) {
	setSlaveAddress(ubAddr);

	ssize_t BytesRead = ::read(m_I2cHandle, pDest, ulReadSize);
	if(BytesRead < 0) {
		throw std::runtime_error("i2c: read from 0x" + std::to_string(ubAddr) +
								" failed: " + std::string(strerror(errno)));
	}
	if(static_cast<uint32_t>(BytesRead) != ulReadSize) {
		throw std::runtime_error("i2c: short read from 0x" + std::to_string(ubAddr) +
								" read " + std::to_string(BytesRead) +
								" expected " + std::to_string(ulReadSize));
	}
}

tI2c::~tI2c() {
	if(m_I2cHandle >= 0) {
		close(m_I2cHandle);
		m_I2cHandle = -1;
	}
}
