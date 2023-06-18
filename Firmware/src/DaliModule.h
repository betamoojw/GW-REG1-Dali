#include <Arduino.h>
#include "OpenKNX.h"
#include "Dali.h"
#include "MessageQueue.h"
#include "StandardChannel.h"
#include "StaircaseChannel.h"

#define DALI_TX 17
#define DALI_RX 16

class DaliModule : public OpenKNX::Module
{
	public:
		void loop() override;
		void loop1() override;
		void setup() override;
		bool usesDualCore() override;
		void processInputKo(GroupObject &ko) override;

		const std::string name() override;
		const std::string version() override;

		bool getDaliBusState();

		bool processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength) override;
		bool processFunctionPropertyState(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength) override;

	private:
		bool _daliBusState = false;
		int _daliStateCounter = 0;
		DaliClass *dali;
		OpenKNX::Channel *channels[64];
		OpenKNX::Channel *groups[16];
		MessageQueue *queue;
		uint8_t sendMsg(MessageType t, byte addr, bool isGroup, byte v, bool wait = false);
};