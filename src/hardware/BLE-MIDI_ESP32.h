#pragma once

// Headers for ESP32 BLE
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

BEGIN_BLEMIDI_NAMESPACE

class BLEMIDI_ESP32
{
private:
    BLEServer*			_server = nullptr;
    BLEAdvertising*		_advertising = nullptr;
    BLECharacteristic*	_characteristic = nullptr;
        
	BLEMIDI<class BLEMIDI_ESP32>* _bleMidiTransport = nullptr;

public:
	BLEMIDI_ESP32()
    {
    }
    
	bool begin(const char*, BLEMIDI<class BLEMIDI_ESP32>*);
    
    void write(uint8_t* buffer, size_t length)
    {
        _characteristic->setValue(buffer, length);
        _characteristic->notify();
    }
    
    /*
    The general form of a MIDI message follows:
    n-byte MIDI Message
    Byte 0            MIDI message Status byte, Bit 7 is Set to 1.
    Bytes 1 to n-1    MIDI message Data bytes, if n > 1. Bit 7 is Set to 0
    There are two types of MIDI messages that can appear in a single packet: full MIDI messages and
    Running Status MIDI messages. Each is encoded differently.
    A full MIDI message is simply the MIDI message with the Status byte included.
    A Running Status MIDI message is a MIDI message with the Status byte omitted. Running Status
    MIDI messages may only be placed in the data stream if the following criteria are met:
    1.  The original MIDI message is 2 bytes or greater and is not a System Common or System
    Real-Time message.
    2.  The omitted Status byte matches the most recently preceding full MIDI message’s Status
    byte within the same BLE packet.
    In addition, the following rules apply with respect to Running Status:
    1.  A Running Status MIDI message is allowed within the packet after at least one full MIDI
    message.
    2.  Every MIDI Status byte must be preceded by a timestamp byte. Running Status MIDI
    messages may be preceded by a timestamp byte. If a Running Status MIDI message is not
    preceded by a timestamp byte, the timestamp byte of the most recently preceding message
    in the same packet is used.
    3.  System Common and System Real-Time messages do not cancel Running Status if
    interspersed between Running Status MIDI messages. However, a timestamp byte must
    precede the Running Status MIDI message that follows.
    4.  The end of a BLE packet does cancel Running Status.
    In the MIDI 1.0 protocol, System Real-Time messages can be sent at any time and may be
    inserted anywhere in a MIDI data stream, including between Status and Data bytes of any other
    MIDI messages. In the MIDI BLE protocol, the System Real-Time messages must be deinterleaved
    from other messages – except for System Exclusive messages.
    */
	void receive(uint8_t* buffer, size_t length)
	{
        // Pointers used to search through payload.
        uint8_t lPtr = 0;
        uint8_t rPtr = 0;
        // lastStatus used to capture runningStatus
        uint8_t lastStatus;
        // Decode first packet -- SHALL be "Full MIDI message"
        lPtr = 2; //Start at first MIDI status -- SHALL be "MIDI status"
        
        //While statement contains incrementing pointers and breaks when buffer size exceeded.
        while (true)
        {
            lastStatus = buffer[lPtr];
            
            if( (buffer[lPtr] < 0x80))
                return; // Status message not present, bail

            // Point to next non-data byte
            rPtr = lPtr;
            while( (buffer[rPtr + 1] < 0x80) && (rPtr < (length - 1)) )
                rPtr++;
            if (buffer[rPtr + 1] == 0xF7) rPtr++;

            // look at l and r pointers and decode by size.
            if( rPtr - lPtr < 1 ) {
                // Time code or system
                xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr], portMAX_DELAY);
            } else if( rPtr - lPtr < 2 ) {
                 xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr], portMAX_DELAY);
                 xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr + 1], portMAX_DELAY);
            } else if( rPtr - lPtr < 3 ) {
                 xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr], portMAX_DELAY);
                 xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr + 1], portMAX_DELAY);
                 xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr + 2], portMAX_DELAY);
            } else {
                // Too much data
                // If not System Common or System Real-Time, send it as running status
                switch(buffer[lPtr] & 0xF0)
                {
                case 0x80:
                case 0x90:
                case 0xA0:
                case 0xB0:
                case 0xE0:
                    for (auto i = lPtr; i < rPtr; i = i + 2)
                    {
                        xQueueSend(_bleMidiTransport->mRxQueue, &buffer[i], portMAX_DELAY);
                        xQueueSend(_bleMidiTransport->mRxQueue, &buffer[i + 1], portMAX_DELAY);
                        xQueueSend(_bleMidiTransport->mRxQueue, &buffer[i + 2], portMAX_DELAY);
                    }
                    break;
                case 0xC0:
                case 0xD0:
                    for (auto i = lPtr; i < rPtr; i = i + 1)
                    {
                        xQueueSend(_bleMidiTransport->mRxQueue, &buffer[i], portMAX_DELAY);
                        xQueueSend(_bleMidiTransport->mRxQueue, &buffer[i + 1], portMAX_DELAY);
                    }
                    break;
                case 0xF0:
                    xQueueSend(_bleMidiTransport->mRxQueue, &buffer[lPtr], portMAX_DELAY);
                    for (auto i = lPtr; i < rPtr; i++)
                        xQueueSend(_bleMidiTransport->mRxQueue, &buffer[i + 1], portMAX_DELAY);
                    break;
                default:
                    break;
                }
            }
            
            // Point to next status
            lPtr = rPtr + 2;
            if(lPtr >= length)
                return; //end of packet
        }
	}

	void connected()
	{
		if (_bleMidiTransport->_connectedCallback)
			_bleMidiTransport->_connectedCallback();
	}

	void disconnected()
	{
		if (_bleMidiTransport->_disconnectedCallback)
			_bleMidiTransport->_disconnectedCallback();
	}
};

class MyServerCallbacks: public BLEServerCallbacks {
public:
    MyServerCallbacks(BLEMIDI_ESP32* bluetoothEsp32)
        : _bluetoothEsp32(bluetoothEsp32) {
    }

protected:
	BLEMIDI_ESP32* _bluetoothEsp32 = nullptr;

    void onConnect(BLEServer*) {
        if (_bluetoothEsp32)
            _bluetoothEsp32->connected();
    };
    
    void onDisconnect(BLEServer*) {
        if (_bluetoothEsp32)
            _bluetoothEsp32->disconnected();
	}
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
public:
    MyCharacteristicCallbacks(BLEMIDI_ESP32* bluetoothEsp32)
        : _bluetoothEsp32(bluetoothEsp32 ) {
    }
    
protected:
	BLEMIDI_ESP32* _bluetoothEsp32 = nullptr;

    void onWrite(BLECharacteristic * characteristic) {
        std::string rxValue = characteristic->getValue();
        if (rxValue.length() > 0) {
			_bluetoothEsp32->receive((uint8_t *)(rxValue.c_str()), rxValue.length());
        }
    }
};

bool BLEMIDI_ESP32::begin(const char* deviceName, BLEMIDI<class BLEMIDI_ESP32>* bleMidiTransport)
{
	_bleMidiTransport = bleMidiTransport;

    BLEDevice::init(deviceName);
    
    _server = BLEDevice::createServer();
    _server->setCallbacks(new MyServerCallbacks(this));
    
    // Create the BLE Service
    auto service = _server->createService(BLEUUID(SERVICE_UUID));
    
    // Create a BLE Characteristic
    _characteristic = service->createCharacteristic(
                                                     BLEUUID(CHARACTERISTIC_UUID),
                                                     BLECharacteristic::PROPERTY_READ   |
                                                     BLECharacteristic::PROPERTY_WRITE  |
                                                     BLECharacteristic::PROPERTY_NOTIFY |
                                                     BLECharacteristic::PROPERTY_WRITE_NR
                                                     );
    // Add CCCD 0x2902 to allow notify
    _characteristic->addDescriptor(new BLE2902());

    _characteristic->setCallbacks(new MyCharacteristicCallbacks(this));
    // Start the service
    service->start();
    
    auto advertisementData = BLEAdvertisementData();
    advertisementData.setFlags(0x04);
    advertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));
    advertisementData.setName(deviceName);

    // Start advertising
    _advertising = _server->getAdvertising();
    _advertising->setAdvertisementData(advertisementData);
    _advertising->start();
    
    return true;
}

END_BLEMIDI_NAMESPACE
