
#pragma once

#include "CommChannels.h"
#include "rack.hpp"



//namespace rack::logger = ::rack::logger;

namespace rack {
    namespace logger =  ::rack::logger;
};

/**
 * This class manages the communication between
 * misers and mixer expanders.
 * 
 * The following if for the first version of the mixers. It's all different now.
 * 
 * How does threading work?
 * We receive solo requests from the UI on the UI thread, but queue those up
 * to handle on the audio thread.
 * 
 * All solo handling goes on on the audio thread, whether if came from our own UI
 * or from a different module over the expansion bus.
 * 
 * Modules just send SOLO commands to toggle themselve, and we re-interpret them
 * as requests to solo or un-solo. But can we do that with multi solos?
 * 
 * And should be be doing this on the audio thread at all? 
 * We can just as easily do it from the UI.
 * 
 * 
 * What happens for various user actions?
 * 
 * exclusive solo on a non-solo channel:
 *      solo self
 *      un-solo everyone else in self
 *      turn off all other modules.
 * 
 * exclusive solo on a solo channel:
 *      un-solo all channels in self
 *      turn on all modules
 * 
 * multi-solo on a non-solo channel
 *      solo self
 *      mute other modules unless they have a soloed channel
 * 
 * multi-colo on a soloed channel
 *      un-solo current channel
 *      perhaps turn off self (if other module is soloing)
 *      
 */
class MixerModule : public ::rack::engine::Module
{
public:
    MixerModule();

    void process(const ProcessArgs &args) override;
    virtual void internalProcess() = 0;

    /**
     * Master module will override this to return true.
     * Let's us (base class) know if we are master or slave.
     */
    virtual bool amMaster() { return false; }

    std::shared_ptr<SharedSoloState> getSharedSoloState() {
        return sharedSoloState;
    }
   
    int getModuleIndex()
    {
        return moduleIndex;
    }

    virtual int getNumGroups() const = 0;
    virtual int getMuteAllParam() const = 0;
    virtual int getSolo0Param() const = 0;

    void sendSoloChangedMessageOnAudioThread()
    {
        pleaseSendSoloChangedMessageOnAudioThread = true;
    }
protected:

    /**
     * Concrete subclass overrides these to transfer audio
     * with neighbors. subclass will fill input and consume output
     * on its process call.
     */
    virtual void setExternalInput(const float*)=0;
    virtual void setExternalOutput(float*)=0;

      // only master should call this
    void allocateSharedSoloState();

private:
    /**
     * Expanders provide the buffers to talk to (send data to) the module to their right.
     * that module may be a master, or another expander. 
     * 
     * #1) Send data to right: use you own right producer buffer.
     * #2) Receive data from left:  use left's right consumer buffer
     * 
     * #3) Send data to the left: use your own left producer buffer.
     * #4) Receive data from right: user right's left consumer buffer 
     */
    float bufferFlipR[comBufferSizeRight] = {0};
    float bufferFlopR[comBufferSizeRight] = {0};
    float bufferFlipL[comBufferSizeLeft] = {0};
    float bufferFlopL[comBufferSizeLeft] = {0};

    CommChannelSend sendRightChannel;
    CommChannelSend sendLeftChannel;
    CommChannelReceive receiveRightChannel;
    CommChannelReceive receiveLeftChannel;

    // This guy holds onto a shared solo state, and we pass weak pointers to
    // him to clients so they can work. Only allocated by master.
    std::shared_ptr<SharedSoloStateOwner> sharedSoloStateOwner;
    
    // Only for master. This is used to send ping message down.
    // It is dynamically allocated, but never freed (it leaks).
    // We can't free it, because we can't know if it is still on use to
    // send a message down the bus.
    SharedSoloStateClient* stateForClient = nullptr;

    // both masters and expanders hold onto these.
    std::shared_ptr<SharedSoloState> sharedSoloState;

    bool pleaseSendSoloChangedMessageOnAudioThread = false;

    /**
     * module index is the mixer's identity. master is always 0, then it increases by one 
     * going right to left. module index is the index for "me" in stateForClient.
     */
    int moduleIndex = 0;
    int pingDelayCount = 0;

    bool haveInitSoloState = false;

    void processMessageFromBus(const CommChannelMessage& msg, bool isEndOfMessageChain);
    void pollForModulePing(bool pairedLeft);
    void onSomethingChanged();
    void initSoloState();

};

#if 0
inline void dumpState(const char* title,  std::shared_ptr<SharedSoloState> state) {
    DEBUG("    state: %s", title);
    DEBUG("        excl=%d,%d,%d", 
        !!state->state[0].exclusiveSolo,
        !!state->state[1].exclusiveSolo,
        !!state->state[2].exclusiveSolo);
    DEBUG("        multi=%d,%d,%d", 
        !!state->state[0].multiSolo,
        !!state->state[1].multiSolo,
        !!state->state[2].multiSolo);
}
#endif

// Remember : "rightModule" is the module to your right
// producer and consumer are concepts of the engine, not us.
// rack will flip producer and consumer each process tick.
inline MixerModule::MixerModule()
{
    // rightExpander is a field from rack::Module.
    rightExpander.producerMessage = bufferFlipR;
    rightExpander.consumerMessage = bufferFlopR;
    leftExpander.producerMessage = bufferFlipL;
    leftExpander.consumerMessage = bufferFlopL;
}

inline void MixerModule::allocateSharedSoloState()
{
    assert(!sharedSoloStateOwner);
    sharedSoloStateOwner = std::make_shared<SharedSoloStateOwner>();
    sharedSoloState =  sharedSoloStateOwner->state; 
    stateForClient = new SharedSoloStateClient(sharedSoloStateOwner);
   // initSoloState();        // master can do this right now, and only check once.
}

inline void MixerModule::pollForModulePing(bool pairedLeft)
{
    // Only master does this. only master has sharedSoloStateOwner
    if (amMaster()) {
        if (pingDelayCount-- <= 0) {
            initSoloState();
            pingDelayCount = 100;       // poll every 100 samples?
            //pingDelayCount = 100000;        // for debugging, do less often
            assert(sharedSoloStateOwner);
            assert(stateForClient);

            // now, if paired left, send a message to the right
            if (pairedLeft) {
                assert(moduleIndex == 0);
                stateForClient->moduleNumber = 1;
                CommChannelMessage msg;
                msg.commandId = CommCommand_SetSharedState;
                msg.commandPayload = size_t(stateForClient);
                sendLeftChannel.send(msg);

               // WARN("setting shared solo from master %d", !!sharedSoloState);
            }
        }
    }
}

inline void MixerModule::initSoloState() {
    if (!sharedSoloState) {
        WARN("can't init solo yet");
        return;
    }
    if (!haveInitSoloState) {
        haveInitSoloState = true;

        DEBUG("Init module %d", moduleIndex);

        bool moduleHasSolo = false;
        for (int group = 0; group < this->getNumGroups(); ++group ) {
            const int soloParamNum =  this->getSolo0Param() + group;
            const bool groupIsSoloing = ::rack::APP->engine->getParam(this, soloParamNum);
            moduleHasSolo |= groupIsSoloing;
        }

        sharedSoloState->state[moduleIndex].exclusiveSolo = false;
        sharedSoloState->state[moduleIndex].multiSolo = moduleHasSolo;
        // dumpState("   after init", sharedSoloState);
    }
}

inline void MixerModule::processMessageFromBus(const CommChannelMessage& msg, bool isEndOfMessageChain)
{
    // TODO: why are we getting this?
    if (msg.commandId == 0) {
        // WARN("blowing off ");
        return;
    }

    switch(msg.commandId) {
        case CommCommand_SetSharedState:
            {
                SharedSoloStateClient* stateForClient = reinterpret_cast<SharedSoloStateClient*>(msg.commandPayload);
                std::shared_ptr<SharedSoloStateOwner> owner = stateForClient->owner.lock();

                // If then owner has been deleted, then bail
                if (!owner) {
                    sharedSoloState.reset();
                    return;
                }

                sharedSoloState = owner->state;
                moduleIndex = stateForClient->moduleNumber++;
                initSoloState();        // make sure we have initialized.
            }
            break;
        case CommCommand_SomethingChanged:
            onSomethingChanged();
            break;
        default:
            WARN("no handler for message %x", msg.commandId);
    }
}

// Each time we are called, we want the unit on the left to output
// all bus audio to the unit on its right.
// So - expander on the left will output to it's producer buffer.
// And - master on the right will get it's bus input from the consumer buffer 
//      from the unit on its right

inline void MixerModule::process(const ProcessArgs &args)
{

    // first, determine what modules are are paired with what
    // A Mix4 is not a master, and can pair with either a Mix4 or a MixM to the right
    const bool pairedRight = rightExpander.module && 
        (   (rightExpander.module->model == modelMixMModule) ||
            (rightExpander.module->model == modelMix4Module) ||
            (rightExpander.module->model == modelMixStereoModule)
        ) &&
        !amMaster();

    // A MixM and a Mix4 can both pair with a Mix4 to the left
    const bool pairedLeft = leftExpander.module &&
        ((leftExpander.module->model == modelMix4Module) ||
        (leftExpander.module->model == modelMixStereoModule));

    // recently ported these asserts. Hope they are right.
    assert(rightExpander.producerMessage);
    assert(!pairedLeft || leftExpander.module->rightExpander.consumerMessage);

    // set a channel to send data to the right (case #1, above)
    setExternalOutput(pairedRight ? reinterpret_cast<float *>(rightExpander.producerMessage) : nullptr);
    
    // set a channel to rx data from the left (case #2, above)
    setExternalInput(pairedLeft ? reinterpret_cast<float *>(leftExpander.module->rightExpander.consumerMessage) : nullptr);

    pollForModulePing(pairedLeft);

    if (pairedRight) {
        // #1) Send data to right: use you own right producer buffer.
        uint32_t* outBuf = reinterpret_cast<uint32_t *>(rightExpander.producerMessage);

        // #4) Receive data from right: user right's left consumer buffer
        const uint32_t* inBuf = reinterpret_cast<uint32_t *>(rightExpander.module->leftExpander.consumerMessage);
        
        sendRightChannel.go(
            outBuf + comBufferRightCommandIdOffset,
            (size_t*)(outBuf + comBufferRightCommandDataOffset));

        CommChannelMessage msg;
        const bool isCommand = receiveRightChannel.rx(
            inBuf + comBufferLeftCommandIdOffset,
            (size_t*)(inBuf + comBufferLeftCommandDataOffset),
            msg);

        if (isCommand) {
            processMessageFromBus(msg, !pairedLeft);
            // now relay down to the left
            if (pairedLeft) {
                 sendLeftChannel.send(msg);
            }
        } // else WARN("got no command from right");
        if (pleaseSendSoloChangedMessageOnAudioThread) {
            CommChannelMessage msg2;
            msg2.commandId = CommCommand_SomethingChanged;
            sendRightChannel.send(msg2);
        }
    }

    if (pairedLeft) {
        // #3) Send data to the left: use your own left producer buffer.
        uint32_t* outBuf = reinterpret_cast<uint32_t *>(leftExpander.producerMessage);
        
        // #2) Receive data from left:  use left's right consumer buffer
        const uint32_t* inBuf = reinterpret_cast<uint32_t *>(leftExpander.module->rightExpander.consumerMessage);
        
        sendLeftChannel.go(
            outBuf + comBufferLeftCommandIdOffset,
            (size_t*)(outBuf + comBufferLeftCommandDataOffset));
        
        CommChannelMessage msg;
        const bool isCommand = receiveLeftChannel.rx(
            inBuf + comBufferRightCommandIdOffset,
            (size_t*)(inBuf + comBufferRightCommandDataOffset),
            msg);
        if (isCommand) {
            processMessageFromBus(msg, !pairedRight);
            if (pairedRight) {
                 sendRightChannel.send(msg);
            }
        }
        if (pleaseSendSoloChangedMessageOnAudioThread) {
            CommChannelMessage msg2;
            msg2.commandId = CommCommand_SomethingChanged;
            sendLeftChannel.send(msg2);
        }
    }
  
    pleaseSendSoloChangedMessageOnAudioThread = false;
    // Do the audio processing, and handle the left and right audio buses
    internalProcess();

    if (pairedRight) {
        rightExpander.messageFlipRequested = true;
    }
    if (pairedLeft) {
        leftExpander.messageFlipRequested = true;
    }
}




// called from audio thread
inline void MixerModule::onSomethingChanged()
{
  //  DEBUG("on something changed");
    if (!sharedSoloState) {
        WARN("something changed, but no state");
        return;
    }

    if (moduleIndex >= SharedSoloState::maxModules) {
        WARN("too many modules");
        return;
    }

    //dumpState("start of something changed ", sharedSoloState);

    bool otherModuleHasSolo = false;
    bool thisModuleHasSolo = false;
    bool otherModuleHasExclusiveSolo = false;
    for (int i=0; i<SharedSoloState::maxModules; ++i) {
        if (i != moduleIndex) {
            otherModuleHasSolo |= sharedSoloState->state[i].exclusiveSolo;
            otherModuleHasSolo |= sharedSoloState->state[i].multiSolo;
            otherModuleHasExclusiveSolo |= sharedSoloState->state[i].exclusiveSolo;
        } else {
            thisModuleHasSolo |= sharedSoloState->state[i].exclusiveSolo;
            thisModuleHasSolo |= sharedSoloState->state[i].multiSolo;
        }
     }
     // case : I have multi and other guy has exclusive

#if 0
     DEBUG("    on-ch, thissolo = %d othersolo = %d otherexclusive = %d\n",
        thisModuleHasSolo, 
        otherModuleHasSolo,
        otherModuleHasExclusiveSolo);
#endif

    engine::Engine* eng = ::rack::APP->engine;

    const bool thisModuleShouldMute = 
        (otherModuleHasSolo && !thisModuleHasSolo) ||
        otherModuleHasExclusiveSolo;

    //DEBUG("    thisModuleShouldMute = %d", thisModuleShouldMute);

    eng->setParam(this, getMuteAllParam(), thisModuleShouldMute ? 1.f : 0.f); 

    if (otherModuleHasExclusiveSolo) {
       // DEBUG("    on something changed - other module has exclusive so clear our solos");
        for (int i=0; i< this->getNumGroups(); ++i ) {
            const int paramNum =  this->getSolo0Param() + i;
            eng->setParam(this, paramNum, 0.f);
        }
    }

    // dumpState("leave something changed ", sharedSoloState);
}

/********************************************************
 * Support function added here for convenience.
 * Put in their own namespace: sqmix.
 */

namespace sqmix {


/**
 * my current thinking is that we should update all the params for our module here,
 * and update the share solo state.
 * then we just need to ding the mixer module to send the changed message
 */
template<class Comp>
inline void handleSoloClickFromUI(MixerModule* mixer, int channel, bool ctrl)
{
   
    auto state = mixer->getSharedSoloState();
    int myIndex = mixer->getModuleIndex();
    if (!state) {
        WARN("can't get shared state");
        return;
    }
    if (myIndex >= SharedSoloState::maxModules) {
        WARN("too many modules");
        return;
    }
    
    // DEBUG("** handleSoloClickFromUI, ctrl = %d myIndex = %d\n", ctrl, myIndex);
    // only worry about exclusive, for now
    const int channelParamNum =  Comp::SOLO0_PARAM + channel;

    // before processing this button - is the solo on?
    const bool groupIsSoloing = ::rack::APP->engine->getParam(mixer, channelParamNum);
    const bool groupIsSoloingAfter = !groupIsSoloing;
    engine::Engine* eng = ::rack::APP->engine;

    // if any of our groups are soloing, the module must be
    bool moduleIsSoloingAfter = groupIsSoloingAfter;
    for (int i=0; i< mixer->getNumGroups(); ++i ) {
        const int paramNum =  Comp::SOLO0_PARAM + i;
        if (i == channel) {
            // toggle the one we clicked on
            eng->setParam(mixer, paramNum, groupIsSoloing ? 0.f : 1.f);
        } else {
          
            if (!ctrl) {
                // if it's exclusive, turn off other channels
                eng->setParam(mixer, paramNum, 0.f);     
            } else {
                const bool soloing = ::rack::APP->engine->getParam(mixer, paramNum);
                moduleIsSoloingAfter |= soloing;
            }
        }
    }

    // now update the shared state. If we are exclusive we must
    // clear others and set ours.
    // if we were exclusive and un-soloed, we must clear ourlf

    const bool isExclusive = !ctrl;
#if 0
    DEBUG("    groupIsSoloingAfter = %d isExclusive = %d moduleSolo=%d", 
        groupIsSoloingAfter, 
        isExclusive,
        moduleIsSoloingAfter);
    dumpState("before update", state);
#endif

    bool otherModulesHaveMutes = false;
    for (int i=0; i<SharedSoloState::maxModules; ++i) {
        const bool isMe = (i == myIndex);
        if (isMe) {
            if (isExclusive) {
                // set our own exclusive state to match our solo.
                state->state[i].exclusiveSolo = groupIsSoloingAfter;
                state->state[i].multiSolo = false;
                // DEBUG("set multi[%d] false because isExclusive", i);
            } else {
                state->state[i].exclusiveSolo = false;
                state->state[i].multiSolo = moduleIsSoloingAfter;
                // DEBUG("set multi[%d] to %d because moduleIsSoloingAfter flag", i, !!state->state[i].multiSolo);
            }
        }

        if (!isMe && isExclusive && groupIsSoloingAfter) {
            // if we just enabled and exclusive solo, clear everyone else.
            // DEBUG("   clearing exclusive in module %d", i);
            state->state[i].exclusiveSolo = false;
        }

        if (!isMe) {
            otherModulesHaveMutes |= state->state[i].exclusiveSolo;
            otherModulesHaveMutes |= state->state[i].multiSolo;
        }
    }

    // DEBUG("   otherModulesHaveMutes = %d", otherModulesHaveMutes);

    // un-mute yourself if there are no solos in other module, 
    // or if this module has solo.
    // BUT - should also mute if other module is soloing and we are not
    bool shouldMuteSelf = false;
    if (!otherModulesHaveMutes || moduleIsSoloingAfter) {
        shouldMuteSelf = false;
    }

    if (otherModulesHaveMutes && !moduleIsSoloingAfter) {
        shouldMuteSelf = true;
    }

    // DEBUG("   shouldMuteSelf %d", shouldMuteSelf);
    eng->setParam(mixer, Comp::ALL_CHANNELS_OFF_PARAM, shouldMuteSelf ? 1.f : 0.f); 

    //dumpState("after update", state);
    mixer->sendSoloChangedMessageOnAudioThread();
}
}   // end namespace
