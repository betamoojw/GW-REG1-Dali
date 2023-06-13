#include "OpenKNX.h"
#include "Dali.h"
#include "MessageQueue.h"

class StandardChannel : public OpenKNX::Channel
{
	public:
        StandardChannel(uint8_t channelIndex, MessageQueue *queue, bool isGroup);
        ~StandardChannel();

		void loop() override;
		void loop1() override;
		void setup() override;
		void processInputKo(GroupObject &ko) override;

		const std::string name() override;
		// void writeFlash() override;
		// void readFlash(const uint8_t* data, const uint16_t size) override;
		// uint16_t flashSize() override;

	private:
		MessageQueue *_queue;
		bool isGroup = false;
};