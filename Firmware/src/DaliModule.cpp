#include "DaliModule.h"

const std::string DaliModule::name()
{
    return "Dali";
}

//You can also give it a version
//will be displayed in Command Infos 
const std::string DaliModule::version()
{
    return "0.0dev";
}

#ifdef USE_TINYUSB
void DaliModule::setCallback(EventHandlerReceivedDataFuncPtr callback)
{
    dali->setCallback(callback);
}
#endif

//will be called once
//only if knx.configured == true
void DaliModule::setup()
{
    queue = new MessageQueue();

    for(int i = 0; i < 64; i++)
    {
        DaliChannel *ch = new DaliChannel(i, queue, false);
        ch->setup();
        channels[i] = ch;
    }

    
    for(int i = 0; i < 16; i++)
    {
        DaliChannel *ch = new DaliChannel(i, queue, true);
        ch->setup();
        groups[i] = ch;
    }
}

void DaliModule::setup1()
{
    dali = new DaliClass();
	dali->begin(DALI_TX, DALI_RX);
}

void DaliModule::loop()
{
    if(_adrState != AddressingState::None) return;

    for(int i = 0; i < 64; i++)
    {
        channels[i]->loop();
    }
    for(int i = 0; i < 16; i++)
    {
        groups[i]->loop();
    }
}

void DaliModule::loop1()
{
    if(_adrState != AddressingState::None)
    {
        loopAddressing();
        loopMessages();
        return;
    }

    for(int i = 0; i < 64; i++)
    {
        channels[i]->loop1();
    }
    for(int i = 0; i < 16; i++)
    {
        groups[i]->loop1();
    }

    loopMessages();
}

void DaliModule::loopMessages()
{
    Message *msg = queue->pop();
    if(msg == nullptr) return;

    switch(msg->type)
    {
        case MessageType::Arc:
        {
            int16_t resp = dali->sendArcWait(msg->para1, msg->para2, msg->addrtype);
            if(msg->wait)
                queue->setResponse(msg->id, resp);
            break;
        }

        case MessageType::Cmd:
        {
            int16_t resp = dali->sendCmdWait(msg->para1, msg->para2, msg->addrtype);
            if(msg->wait)
            {
                //logInfoP("Got Response %i = %i", msg->id, resp);
                queue->setResponse(msg->id, resp);
            }
            break;
        }

        case MessageType::SpecialCmd:
        {
            int16_t resp = dali->sendSpecialCmdWait(msg->para1, msg->para2);
            if(msg->wait)
            {
                queue->setResponse(msg->id, resp);
            }
        }
    }

    delete[] msg;
}

void DaliModule::loopAddressing()
{
    switch(_adrState)
    {
        case AddressingState::Randomize_Wait:
        {
            if(millis() - _adrTime > DALI_WAIT_RANDOMIZE)
            {
                _adrState = AddressingState::Search;
                logInfoP("RandomizeWait finished");
            }
            break;
        }

        case AddressingState::Search:
        {
            if(_adrNoRespCounter == 2)
            {
                _adrState = AddressingState::Finish;
                break;
            }

            byte high = _adrHigh >> 16;
            byte middle = (_adrHigh >> 8) & 0xFF;
            byte low = _adrHigh & 0xFF;
            sendCmdSpecial(dali->CMD_SEARCHADDRH, high);
            sendCmdSpecial(dali->CMD_SEARCHADDRM, middle);
            sendCmdSpecial(dali->CMD_SEARCHADDRL, low);
            _adrResp = sendCmdSpecial(dali->CMD_COMPARE, 0x00, true);
            _adrTime = millis();
            _adrState = AddressingState::SearchWait;
            break;
        }

        case AddressingState::SearchWait:
        {
            int16_t response = queue->getResponse(_adrResp);
            
            if(response == -200 || response == -1)
            {
                if(response == -1 || millis() - _adrTime > DALI_WAIT_SEARCH)
                {
                    _adrLow = _adrHigh + 1;
                    _adrHigh = _adrHighLast;
                    _adrState = AddressingState::Search;
                    _adrNoRespCounter++;
                }
            } else if(response >= 0) {
                if(_adrLow == _adrHigh)
                {
                    logInfoP("Found Ballast at %X", _adrLow);
                    if(_adrAssign)
                    {
                        uint8_t addr = 0;
                        while(addresses[addr]){
                            addr++;
                        }
                        addresses[addr] = true;
                        logInfoP("Assinging Address %i", addr);
                        sendCmdSpecial(dali->CMD_PROGRAMSHORT, _adrLow);
                        sendCmdSpecial(dali->CMD_WITHDRAW);
                        ballasts[_adrFound].high = (_adrLow >> 16) & 0xFF;
                        ballasts[_adrFound].middle = (_adrLow >> 8) & 0xFF;
                        ballasts[_adrFound].low = _adrLow & 0xFF;
                        ballasts[_adrFound].address = response >> 1;
                        _adrFound++;

                        _adrLow = 0;
                        _adrHigh = 0xFFFFFF;
                        _adrHighLast = 0xFFFFFF;
                        _adrNoRespCounter = 1;
                        sendCmdSpecial(dali->CMD_WITHDRAW);
                        _adrState = AddressingState::Search;
                    } else {
                        _adrResp = sendCmdSpecial(dali->CMD_QUERY_SHORT, 0, true);
                        _adrTime = millis();
                        _adrState = AddressingState::Found;
                    }
                } else {
                    //logInfoP("Range has ballast");
                    _adrHighLast = _adrHigh;
                    _adrHigh = (_adrLow + _adrHigh) / 2;
                    _adrNoRespCounter = 0;
                    _adrState = AddressingState::Search;
                }
            } else {
                logErrorP("Dali Error %i, aborting addressing", response);
                _adrState = AddressingState::Finish;
            }
            break;
        }

        case AddressingState::Found:
        {
            int16_t response = queue->getResponse(_adrResp);
            if(response == -200 || response == -1)
            {
                if(response == -1 || millis() - _adrTime > DALI_WAIT_SEARCH)
                {
                    logErrorP("Found ballast not answering");
                    _adrState = AddressingState::Finish;
                    ballasts[_adrFound].address = 255;
                    _adrFound++;
                }
            } else if(response >= 0) {
                logInfoP("Ballast has Short Address %i", response >> 1);

                ballasts[_adrFound].high = (_adrLow >> 16) & 0xFF;
                ballasts[_adrFound].middle = (_adrLow >> 8) & 0xFF;
                ballasts[_adrFound].low = _adrLow & 0xFF;
                ballasts[_adrFound].address = response >> 1;
                _adrFound++;

                _adrLow = 0;
                _adrHigh = 0xFFFFFF;
                _adrHighLast = 0xFFFFFF;
                _adrNoRespCounter = 1;
                sendCmdSpecial(dali->CMD_WITHDRAW);
                _adrState = AddressingState::Search;
            } else {
                logErrorP("Dali Error %i, aborting addressing", response);
                _adrState = AddressingState::Finish;
            }
            break;
        }

        case AddressingState::Finish:
        {
            logErrorP("Found %i ballasts", _adrFound);
            sendCmdSpecial(dali->CMD_TERMINATE);
            _adrState = AddressingState::None;
            break;
        }

        case AddressingState::Query_Wait:
        {
            int16_t resp = queue->getResponse(_adrResp);

            if(resp == -1 || millis() - _adrTime > DALI_WAIT_SEARCH)
            {
                _adrState = AddressingState::Withdraw_Others;
                logErrorP("Adresse wird nicht verwendet");
                return;
            }

            if(resp == -200) return;

            if(resp >= 0)
            {
                logErrorP("Adresse wird bereits verwendet");
                _assState = AssigningState::Failed_Exists;
                _adrState = AddressingState::None;
                return;
            } else {
                logErrorP("Bus Error %i", resp);
                _assState = AssigningState::Failed_Bus;
                _adrState = AddressingState::None;
                return;
            }
            break;
        }

        case AddressingState::Withdraw_Others:
        {
            sendCmdSpecial(dali->CMD_INITIALISE, 0);
            sendCmdSpecial(dali->CMD_INITIALISE, 0);
            if(_adrHigh > 0)
            {
                logInfoP("Verwerfe alle mit niedrigerer Long Address");
                _adrHigh--;
                byte high = _adrHigh >> 16;
                byte middle = (_adrHigh >> 8) & 0xFF;
                byte low = _adrHigh & 0xFF;
                sendCmdSpecial(dali->CMD_SEARCHADDRH, high);
                sendCmdSpecial(dali->CMD_SEARCHADDRM, middle);
                sendCmdSpecial(dali->CMD_SEARCHADDRL, low);
                sendCmdSpecial(dali->CMD_WITHDRAW);
                _adrHigh++;
            }
            byte high = _adrHigh >> 16;
            byte middle = (_adrHigh >> 8) & 0xFF;
            byte low = _adrHigh & 0xFF;
            sendCmdSpecial(dali->CMD_SEARCHADDRH, high);
            sendCmdSpecial(dali->CMD_SEARCHADDRM, middle);
            sendCmdSpecial(dali->CMD_SEARCHADDRL, low);
            _adrResp =  sendCmdSpecial(dali->CMD_COMPARE, 0, true);
            _adrTime = millis();
            _adrState = AddressingState::Check_Address;
            break;
        }

        case AddressingState::Check_Address:
        {
            if(millis() - _adrTime > DALI_WAIT_SEARCH)
            {
                logInfoP("Gerät antwortet nicht");
                _assState = AssigningState::Failed_No_Answer;
                _adrState = AddressingState::None;
                return;
            }

            int16_t resp = queue->getResponse(_adrResp);

            if(resp == -200) return;

            if(resp >= 0)
            {
                logInfoP("Long Address exists");
                logInfoP("CMD_PROGRAMSHORT to %i", _adrLow);
                sendCmdSpecial(dali->CMD_PROGRAMSHORT, _adrLow);
                _adrState = AddressingState::Confirm_Address;
                _adrResp = sendCmdSpecial(dali->CMD_QUERY_SHORT, 0, true);
                _adrTime = millis();
                logInfoP("Frage Short Address ab");
                return;
            } else if(resp == -1) {
                logInfoP("Long Address dont exists");
                _assState = AssigningState::Failed_Exists_Not;
                _adrState = AddressingState::None;
            } else {
                logInfoP("Bus Error %i", resp);
                _assState = AssigningState::Failed_Bus;
                _adrState = AddressingState::None;
            }
            break;
        }

        case AddressingState::Confirm_Address:
        {
            if(millis() - _adrTime > DALI_WAIT_SEARCH)
            {
                logInfoP("Gerät antwortet nicht");
                _assState = AssigningState::Failed_No_Answer;
                _adrState = AddressingState::None;
                return;
            }

            int16_t resp = queue->getResponse(_adrResp);

            if(resp == -200) return;

            if(resp >= 0)
            {
                logInfoP("Got resp %i", resp);
                if(resp == _adrLow)
                {
                    logInfoP("Adresse erfolgreich gesetzt");
                    _assState = AssigningState::Success;
                } else {
                    logInfoP("Adresse wurde nicht übernommen");
                    _assState = AssigningState::Failed_Confirm;
                }
                _adrState = AddressingState::None;
                return;
            } else {
                logInfoP("Bus Error %i", resp);
                _assState = AssigningState::Failed_Bus;
                _adrState = AddressingState::None;
                return;
            }
            break;
        }

        case AddressingState::SearchAdr:
        {
            if(_adrLow == 64)
            {
                sendCmdSpecial(dali->CMD_INITIALISE, 255);
                sendCmdSpecial(dali->CMD_INITIALISE, 255);
                sendCmdSpecial(dali->CMD_RANDOMISE);
                sendCmdSpecial(dali->CMD_RANDOMISE);
                _adrState = AddressingState::Randomize_Wait;
                _adrLow = 0;
                _adrHigh = 0xFFFFFF;
                _adrHighLast = 0xFFFFFF;
                break;
            }
                
            _adrState = AddressingState::SearchAdrWait;
            _assState = AssigningState::Working;
            _adrResp = sendCmd(_adrLow, dali->CMD_QUERY_STATUS, dali->DALI_SHORT_ADDRESS, true);
            _adrTime = millis();
            break;
        }
        
        case AddressingState::SearchAdrWait:
        {
            if(millis() - _adrTime > DALI_WAIT_SEARCH)
            {
                logInfoP("Address %i dont exists", _adrLow);
                _adrState = AddressingState::SearchAdr;
                _adrLow++;
                return;
            }

            int16_t resp = queue->getResponse(_adrResp);

            if(resp == -200) return;

            if(resp >= 0)
            {
                logInfoP("Address %i exists", _adrLow);
                ballasts[_adrFound].address = _adrLow;
                _adrState = AddressingState::SearchAdr;
                _adrLow++;
            } else if(resp == -1) {
                logInfoP("Address %i dont exists", _adrLow);
                _adrState = AddressingState::SearchAdr;
                _adrLow++;
            } else {
                logInfoP("Bus Error %i", resp);
                _assState = AssigningState::Failed_Bus;
                _adrState = AddressingState::None;
            }
            break;
        }

    }
}

bool DaliModule::getDaliBusState()
{
    return _daliBusState;
}

void DaliModule::processInputKo(GroupObject &ko)
{
    if(_adrState != AddressingState::None) return;

    int koNum = ko.asap();
    if(koNum >= ADR_KoOffset && koNum < ADR_KoOffset + ADR_KoBlockSize * 64)
    {
        int index = floor((koNum - ADR_KoOffset) / ADR_KoBlockSize);
        logInfoP("Got KO %i for CH%i", koNum, index);
        channels[index]->processInputKo(ko);
    }

    if(koNum >= GRP_KoOffset && koNum < GRP_KoOffset + GRP_KoBlockSize * 16)
    {
        int index = floor((koNum - GRP_KoOffset) / GRP_KoBlockSize);
        logInfoP("Got KO %i for G%i", koNum, index);
        groups[index]->processInputKo(ko);
    }

    switch(koNum)
    {
        //broadcast switch
        case APP_Kobroadcast_switch:
        {
            bool value = ko.value(DPT_Switch);
            logInfoP("Broadcast Switch %i", value);
            dali->sendArcBroadcast(value ? 0xFE : 0x00);
            break;
        }

        //broadcast dimm absolute
        case APP_Kobroadcast_dimm:
        {
            uint8_t value = ko.value(Dpt(5,1));
            logInfoP("Broadcast Dimm %i", value);
            value = ((253/3)*(log10(value)+1)) + 1;
            value++;
            dali->sendArcBroadcast(value);
            break;
        }

        //Tag/Nacht Objekt
        case APP_Kodaynight:
        {
            bool value = ko.value(DPT_Switch);
            if(ParamAPP_daynight) value = !value;
            logInfoP("Broadcast Day/Night %i", value);
            if(ParamAPP_daynight) value = !value;

            for(int i = 0; i < 64; i++)
                channels[i]->isNight = value;
            for(int i = 0; i < 16; i++)
                groups[i]->isNight = value;
            break;
        }

        //Set OnValue Day
        case APP_KoonValue:
        {
            uint8_t value = ko.value(Dpt(5,1));
            logDebugP("KO OnValue: %i", value);

            for(int i = 0; i < 64; i++)
                channels[i]->setOnValue(value);
            for(int i = 0; i < 16; i++)
                groups[i]->setOnValue(value);
            break;
        }

        case APP_Koscene:
        {
            uint8_t gotNumber = ko.value(DPT_SceneNumber);
            logDebugP("KO Scene: %i", gotNumber);
            for(int i = 0; i < 16; i++)
            {
                uint8_t dest = ParamSCE_typeIndex(i);
                logDebugP("KO Scene%i: Dest=%i", i, dest);
                if(dest == 0) continue;
                uint8_t number = ParamSCE_numberKnxIndex(i);
                logDebugP("KO Scene%i: Number=%i", i, number-1);
                if(gotNumber == number - 1)
                {
                    bool isSave = ko.value(Dpt(18,1,0));
                    logDebugP("KO Scene%i: Save=%i", i, isSave);
                    if(isSave && !ParamSCE_saveIndex(i))
                    {
                        logDebugP("KO Scene%i: Save not allowed", i);
                        continue;
                    }

                    uint8_t scene = ParamSCE_numberDaliIndex(i);
                    logDebugP("KO Scene%i: Scene=%i", i, scene);
                    uint8_t addr = 0;
                    uint8_t type = 0;
                    switch(dest)
                    {
                        //Address
                        case 1:
                        {
                            addr = ParamSCE_addressIndex(i);
                            logDebugP("KO Scene%i: Addr=%i", i, addr);
                            type = dali->DALI_SHORT_ADDRESS;
                            break;
                        }

                        //Group
                        case 2:
                        {
                            addr = ParamSCE_groupIndex(i);
                            logDebugP("KO Scene%i: Grou=%i", i, addr);
                            type = dali->DALI_GROUP_ADDRESS;
                            break;
                        }

                        //Broadcast
                        case 3:
                        {
                            addr = 0xFF;
                            logDebugP("KO Scene%i: Broadcast", i);
                            type = dali->DALI_GROUP_ADDRESS;
                            break;
                        }
                    }

                    if(isSave)
                    {
                        sendCmd(addr, dali->CMD_ARC_TO_DTR, type);
                        sendCmd(addr, dali->CMD_DTR_AS_SCENE | scene, type);
                    } else {
                        sendCmd(addr, dali->CMD_GO_TO_SCENE | scene, type);
                    }
                }
            }
            break;
        }
    }
}

bool DaliModule::processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    if(objectIndex != 164) return false;

    switch(propertyId)
    {
        case 2:
        {
            uint8_t resp = sendMsg(MessageType::Cmd, data[0], false, dali->CMD_QUERY_DEVICE_TYPE, true);
        
            //todo handle error if dali is not present

            resultData[0] = 0x00;
            resultData[1] = resp;
            resultLength = 2;
            return true;
        }

        case 3:
        {
            ballasts = new Ballast[64];
            logInfoP("Starting Scan");
            _adrState = AddressingState::Init;
            _adrLow = 0;
            _adrHigh = 0xFFFFFF;
            _adrHighLast = 0xFFFFFF;
            _adrFound = 0;
            _adrAssign = false;
            
            sendCmdSpecial(dali->CMD_INITIALISE, data[0] ? 255 : 0);
            sendCmdSpecial(dali->CMD_INITIALISE, data[0] ? 255 : 0);

            if(data[1] == 1)
            {
                sendCmdSpecial(dali->CMD_RANDOMISE);
                sendCmdSpecial(dali->CMD_RANDOMISE);
                _adrState = AddressingState::Randomize_Wait;
                _adrTime = millis();
                logInfoP("RandomizeWait");
            } else {
                logInfoP("Don't Randomize");
                _adrState = AddressingState::Search;
            }


            _adrNoRespCounter = 1;
            resultLength = 0;
            return true;
        }

        case 4:
        {
            logInfoP("Starting assigning address");
            _adrResp = sendCmd(data[0], dali->CMD_QUERY_STATUS, dali->DALI_SHORT_ADDRESS, true);
            _adrTime = millis();
            _adrState = AddressingState::Query_Wait;
            _assState = AssigningState::Working;

            _adrHigh = data[1] << 16;
            _adrHigh |= data[2] << 8;
            _adrHigh |= data[3];
            logInfoP("Long  Addr %X", _adrHigh);
            logInfoP("Short Addr %i", data[0]);
            _adrLow = (data[0] << 1) | 1;
            
            resultLength = 0;
            return true;
        }
        
        case 5:
        {
            logInfoP("Starting addressing new auto");
            _adrLow = 0;
            _adrState = AddressingState::SearchAdr;
            _adrFound = 0;
            _adrAssign = true;
            ballasts = new Ballast[64];
            addresses = new bool[64];
            for(int i = 0; i < 64; i++) addresses[i] = false;
            resultLength = 0;
            return true;
        }
    }


    return false;
}

bool DaliModule::processFunctionPropertyState(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    if(objectIndex != 164) return false;

    switch(propertyId)
    {
        case 1:
        {
            int16_t resp = queue->getResponse(data[0]);
            logInfoP("Check Response %i = %i", data[0], resp);

            if(resp == -255)
            {
                resultData[0] = 0x00;
                resultLength = 1;
                return true;
            } else {
                logInfoP("Send Response %i", resp);
                if(resp >= 0)
                {
                    resultData[0] = 0x01;
                    resultData[1] = (uint8_t)resp;
                } else {
                    resultData[0] = 0x02;
                    resultData[1] = (uint8_t)(resp*-1);
                }
                resultLength = 2;
                return true;
            }
        }

        case 3:
        case 5:
        {
            resultData[0] = data[0] < _adrFound;
            resultData[1] = (data[0] >= _adrFound) && _adrState == AddressingState::None;
            if(data[0] < _adrFound)
            {
                resultData[2] = ballasts[data[0]].high;
                resultData[3] = ballasts[data[0]].middle;
                resultData[4] = ballasts[data[0]].low;
                resultData[5] = ballasts[data[0]].address;
                resultLength = 6;
            } else {
                resultLength = 2;
            }

            if(resultData[1])
            {
                delete[] ballasts;
                delete[] addresses;
            }

            return true;
        }

        case 4:
        {
            resultData[0] = (uint8_t)_assState;
            resultLength = 1;
            return true;
        }
    }
    return false;
}


uint8_t DaliModule::sendArc(byte addr, byte value, byte type)
{
    Message *msg = new Message();
    msg->id = queue->getNextId();
    msg->type = MessageType::Arc;
    msg->para1 = addr;
    msg->para2 = value;
    msg->addrtype = type;
    return queue->push(msg);
}

uint8_t DaliModule::sendCmd(byte addr, byte value, byte type, bool wait)
{
    Message *msg = new Message();
    msg->id = queue->getNextId();
    msg->type = MessageType::Cmd;
    msg->para1 = addr;
    msg->para2 = value;
    msg->addrtype = type;
    msg->wait = wait;
    return queue->push(msg);
}

uint8_t DaliModule::sendCmdSpecial(int command, byte value, bool wait)
{
    Message *msg = new Message();
    msg->id = queue->getNextId();
    msg->type = MessageType::SpecialCmd;
    msg->para1 = command;
    msg->para2 = value;
    msg->addrtype = 0;
    msg->wait = wait;
    return queue->push(msg);
}

uint8_t DaliModule::sendMsg(MessageType t, byte p1, byte p2, byte type, bool wait)
{
    Message *msg = new Message();
    msg->id = queue->getNextId();
    msg->type = t;
    msg->para1 = p1;
    msg->para2 = p2;
    msg->addrtype = type;
    msg->wait = wait;
    return queue->push(msg);
}
