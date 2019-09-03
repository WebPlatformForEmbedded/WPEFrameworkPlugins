#include "BluetoothControl.h"

namespace WPEFramework {

namespace Plugin {

    SERVICE_REGISTRATION(BluetoothControl, 1, 0);

    static Core::ProxyPoolType<Web::JSONBodyType<BluetoothControl::DeviceImpl::JSON>> jsonResponseFactoryDevice(1);
    static Core::ProxyPoolType<Web::JSONBodyType<BluetoothControl::Status>> jsonResponseFactoryStatus(1);
    /* static */ Bluetooth::ControlSocket BluetoothControl::_administrator;

    /* virtual */ const string BluetoothControl::Initialize(PluginHost::IShell* service)
    {
        string result;

        ASSERT(_service == nullptr);
        ASSERT(_administrator.IsOpen() == false);

        _service = service;
        _skipURL = _service->WebPrefix().length();
        _config.FromString(_service->ConfigLine());
        const char* driverMessage = ::construct_bluetooth_driver(_service->ConfigLine().c_str());

        _administrator.DeviceId(_config.Interface.Value());

        // First see if we can bring up the Driver....
        if (driverMessage != nullptr) {
            result = Core::ToString(driverMessage);
        } 
        else {
            Bluetooth::ControlSocket::LinkKeyList linkKeys;
            Bluetooth::ControlSocket::LongTermKeyList longTermKeys;
            // Bluetooth::ControlSocket::IdentityKeyList identityKeys;

            if (Bluetooth::ControlSocket::Up(_administrator.DeviceId()) == false) {
                result = "Could not activate bluetooth interface.";
            }
            else if (_administrator.Power(true) != Core::ERROR_NONE) {
                result = "Failed to power up the bluetooth interface";
            }
            else if (_administrator.SimplePairing(true) != Core::ERROR_NONE) {
                result = "Failed to enable simple pairing on the bluetooth interface";
            }
            else if (_administrator.Bondable(true) != Core::ERROR_NONE) {
                result = "Failed to enable bonding on the bluetooth interface";
            }
            else if (_administrator.LowEnergy(true) != Core::ERROR_NONE) {
                result = "Failed to enable low energy on the bluetooth interface";
            }
            else if (_administrator.Secure(true) != Core::ERROR_NONE) {
                result = "Failed to enable security on the bluetooth interface";
            }
            else if (_administrator.Advertising(true) != Core::ERROR_NONE) {
                result = "Failed to enable advertising on the bluetooth interface";
            }
            else if (_administrator.LinkKeys(linkKeys) != Core::ERROR_NONE) {
                result = "Failed to upload link keys to the bluetooth interface";
            }
            else if (_administrator.LongTermKeys(longTermKeys) != Core::ERROR_NONE) {
                result = "Failed to upload long term keys to the bluetooth interface";
            }
            else if (_administrator.Name(_T("Thunder"), _config.Name.Value()) != Core::ERROR_NONE) {
                result = "Failed to upload identity keys to the bluetooth interface";
            }
            else if (_application.Open(_administrator.DeviceId()) != Core::ERROR_NONE) {
                result = "Could not open the bluetooth application channel";
            } 
            else if (_application.Advertising(true, 0) != Core::ERROR_NONE) {
                result = "Could not listen to advertisements on the Application channel";
            }

            if (result.empty() == false) {
                Bluetooth::ControlSocket::Down(_administrator.DeviceId());
                _administrator.DeviceId(HCI_DEV_NONE);
                _application.Close();
                ::destruct_bluetooth_driver();
            }
        }
        return result;
    }

    /*virtual*/ void BluetoothControl::Deinitialize(PluginHost::IShell* service)
    {
        ASSERT(_service == service);

        // Deinitialize what we initialized..
        _service = nullptr;

        // We bring the interface up, so we should bring it down as well..
        _application.Close();
        Bluetooth::ControlSocket::Down(_administrator.DeviceId());
        _administrator.DeviceId(HCI_DEV_NONE);
        ::destruct_bluetooth_driver();
    }

    /* virtual */ string BluetoothControl::Information() const
    {
        // No additional info to report.
        return (nullptr);
    }

    /* virtual */ void BluetoothControl::Inbound(WPEFramework::Web::Request& request)
    {
        Core::TextSegmentIterator index(Core::TextFragment(request.Path, _skipURL, static_cast<uint32_t>(request.Path.length() - _skipURL)), false, '/');
        // By default, we are in front of any element, jump onto the first element, which is if, there is something an empty slot.
        index.Next();

        if ((request.Verb == Web::Request::HTTP_PUT) || (request.Verb == Web::Request::HTTP_POST) || (request.Verb == Web::Request::HTTP_DELETE)) {
            if ((index.IsValid() == true) && (index.Next() && index.IsValid())) {
                if ((index.Remainder() == _T("Pair")) || (index.Remainder() == _T("Connect")) || (index.Remainder() == _T("Disconnect")))
                    request.Body(jsonResponseFactoryDevice.Element());
            }
        }
    }

    /* virtual */ Core::ProxyType<Web::Response> BluetoothControl::Process(const WPEFramework::Web::Request& request)
    {
        ASSERT(_skipURL <= request.Path.length());

        TRACE(Trace::Information, (string(_T("Received BluetoothControl request"))));

        Core::ProxyType<Web::Response> result;
        Core::TextSegmentIterator index(Core::TextFragment(request.Path, _skipURL, request.Path.length() - _skipURL), false, '/');

        // By default, we are in front of any element, jump onto the first element, which is if, there is something an empty slot.
        index.Next();

        if (request.Verb == Web::Request::HTTP_GET) {

            result = GetMethod(index);
        } else if (request.Verb == Web::Request::HTTP_PUT) {

            result = PutMethod(index, request);
        } else if (request.Verb == Web::Request::HTTP_POST) {

            result = PostMethod(index, request);
        } else if (request.Verb == Web::Request::HTTP_DELETE) {

            result = DeleteMethod(index, request);
        }

        return result;
    }

    Core::ProxyType<Web::Response> BluetoothControl::GetMethod(Core::TextSegmentIterator& index)
    {
        Core::ProxyType<Web::Response> result(PluginHost::Factories::Instance().Response());
        result->ErrorCode = Web::STATUS_BAD_REQUEST;
        result->Message = _T("Unsupported GET request.");

        if (index.Next() == false) {

            Core::ProxyType<Web::JSONBodyType<Status>> response(jsonResponseFactoryStatus.Element());

            result->ErrorCode = Web::STATUS_OK;
            result->Message = _T("Current status.");

            response->Scanning = IsScanning();
            std::list<DeviceImpl*>::const_iterator loop = _devices.begin();

            while (loop != _devices.end()) {
                response->Devices.Add().Set(*loop);
                loop++;
            }

            result->Body(response);
        } else {
            TRACE(Trace::Information, (string(__FUNCTION__)));
            DeviceImpl* device = Find(index.Current().Text());

            if (device != nullptr) {
                Core::ProxyType<Web::JSONBodyType<DeviceImpl::JSON>> response(jsonResponseFactoryDevice.Element());
                response->Set(device);

                result->ErrorCode = Web::STATUS_OK;
                result->Message = _T("device info.");
                result->Body(response);
            } else {
                result->ErrorCode = Web::STATUS_NO_CONTENT;
                result->Message = _T("Unable to display device. Device not found");
            }
        }

        return result;
    }

    Core::ProxyType<Web::Response> BluetoothControl::PutMethod(Core::TextSegmentIterator& index, const Web::Request& request)
    {
        Core::ProxyType<Web::Response> result(PluginHost::Factories::Instance().Response());
        result->ErrorCode = Web::STATUS_BAD_REQUEST;
        result->Message = _T("Unsupported PUT request.");

        if (index.IsValid() == true) {
            if (index.Next()) {
                TRACE(Trace::Information, (string(__FUNCTION__)));

                if (index.Current() == _T("Scan")) {
                    Core::URL::KeyValue options(request.Query.Value());

                    bool lowEnergy = options.Boolean(_T("LowEnergy"), true);
                    bool limited = options.Boolean(_T("Limited"), false);
                    bool passive = options.Boolean(_T("Passive"), false);
                    uint16_t duration = options.Number<uint16_t>(_T("ScanTime"), 10);
                    uint8_t flags = 0;
                    uint32_t type = 0x338B9E;

                    if (lowEnergy == true) {
                        _application.Scan(duration, limited, passive);
                    } else {
                        _application.Scan(duration, type, flags);
                    }
                    result->ErrorCode = Web::STATUS_OK;
                    result->Message = _T("Scan started.");
                } else if ((index.Current() == _T("Pair")) || (index.Current() == _T("Connect"))) {
                    bool pair = (index.Current() == _T("Pair"));
                    string destination;
                    if (index.Next() == true) {
                        destination = index.Current().Text();
                    } else if (request.HasBody() == true) {
                        destination = request.Body<const DeviceImpl::JSON>()->Address.Value();
                    }
                    DeviceImpl* device = Find(destination);
                    if (device == nullptr) {
                        result->ErrorCode = Web::STATUS_NOT_FOUND;
                        result->Message = _T("Device not found.");
                    } else if (pair == true) {
                        if (device->Pair() == Core::ERROR_NONE) {
                            result->ErrorCode = Web::STATUS_OK;
                            result->Message = _T("Paired device.");
                        } else {
                            result->ErrorCode = Web::STATUS_UNPROCESSABLE_ENTITY;
                            result->Message = _T("Unable to Pair device.");
                        }
                    } else if (device->Connect() == Core::ERROR_NONE) {
                        result->ErrorCode = Web::STATUS_OK;
                        result->Message = _T("Connected device.");
                    } else {
                        result->ErrorCode = Web::STATUS_UNPROCESSABLE_ENTITY;
                        result->Message = _T("Unable to connect to device.");
                    }
                } else {
                    result->ErrorCode = Web::STATUS_BAD_REQUEST;
                    result->Message = _T("Unable to process PUT request.");
                }
            }
        }

        return result;
    }

    Core::ProxyType<Web::Response> BluetoothControl::PostMethod(Core::TextSegmentIterator& index, const Web::Request& request)
    {
        Core::ProxyType<Web::Response> result(PluginHost::Factories::Instance().Response());
        result->ErrorCode = Web::STATUS_BAD_REQUEST;
        result->Message = _T("Unsupported POST request.");

        if (index.IsValid() == true) {
            if (index.Next()) {
                TRACE(Trace::Information, (string(__FUNCTION__)));

                if (index.Current() == _T("Remote")) {
                    string address;
                    if (index.Next() == true) {
                        address = index.Current().Text();
                    } else if (request.HasBody() == true) {
                        address = request.Body<const DeviceImpl::JSON>()->Address.Value();
                    }
                    DeviceImpl* device = Find(address);
                    if (device == nullptr) {
                        result->ErrorCode = Web::STATUS_NOT_FOUND;
                        result->Message = _T("Unknown device.");
                    } else {
                        _gattRemotes.emplace_back(0, device->Locator());
                        result->ErrorCode = Web::STATUS_OK;
                        result->Message = _T("Unpaired device.");
                    }
                }
            }
        }

        return result;
    }

    Core::ProxyType<Web::Response> BluetoothControl::DeleteMethod(Core::TextSegmentIterator& index, const Web::Request& request)
    {
        Core::ProxyType<Web::Response> result(PluginHost::Factories::Instance().Response());
        result->ErrorCode = Web::STATUS_BAD_REQUEST;
        result->Message = _T("Unsupported DELETE request.");

        if (index.IsValid() == true) {
            if (index.Next()) {
                TRACE(Trace::Information, (string(__FUNCTION__)));

                if (index.Current() == _T("Scan")) {
                    _application.Abort();
                    result->ErrorCode = Web::STATUS_OK;
                    result->Message = _T("Scan stopped.");
                } else if ((index.Current() == _T("Pair")) || (index.Current() == _T("Connect"))) {
                    bool pair = (index.Current() == _T("Pair"));
                    string address;
                    if (index.Next() == true) {
                        address = index.Current().Text();
                    } else if (request.HasBody() == true) {
                        address = request.Body<const DeviceImpl::JSON>()->Address.Value();
                    }
                    DeviceImpl* device = Find(address);
                    if (device == nullptr) {
                        result->ErrorCode = Web::STATUS_NOT_FOUND;
                        result->Message = _T("Unknown device.");
                    } else if (pair == true) {
                        if (device->Unpair() == Core::ERROR_NONE) {
                            result->ErrorCode = Web::STATUS_OK;
                            result->Message = _T("Unpaired device.");
                        } else {
                            result->ErrorCode = Web::STATUS_UNPROCESSABLE_ENTITY;
                            result->Message = _T("Unable to Unpair device.");
                        }
                    } else {
                        uint16_t reason = 0;
                        if (index.Next() == true) {
                            reason = Core::NumberType<uint16_t>(index.Current()).Value();
                        } else if (request.HasBody() == true) {
                            reason = request.Body<const DeviceImpl::JSON>()->Reason;
                        }

                        if (device->Disconnect(reason) == Core::ERROR_NONE) {
                            result->ErrorCode = Web::STATUS_OK;
                            result->Message = _T("Disconnected device.");
                        } else {
                            result->ErrorCode = Web::STATUS_UNPROCESSABLE_ENTITY;
                            result->Message = _T("Unable to Disconnect device.");
                        }
                    }
                } else {
                    result->ErrorCode = Web::STATUS_BAD_REQUEST;
                    result->Message = _T("Unable to Disconnect device.");
                }
            }
        }

        return result;
    }

    //  IBluetooth methods
    // -------------------------------------------------------------------------------------------------------
    /* virtual */ bool BluetoothControl::IsScanning() const
    {
        return (_application.IsScanning());
    }
    /* virtual */ uint32_t BluetoothControl::Register(IBluetooth::INotification* notification)
    {
        _adminLock.Lock();

        // Make sure a sink is not registered multiple times.
        ASSERT(std::find(_observers.begin(), _observers.end(), notification) == _observers.end());

        _observers.push_back(notification);
        notification->AddRef();

        // Allright iterate over all devices, so thay get announced by the observer..
        for (std::list<DeviceImpl*>::iterator index = _devices.begin(), end = _devices.end(); index != end; ++index) {
            notification->Update(*index);
        }

        _adminLock.Unlock();

        return (Core::ERROR_NONE);
    }
    /* virtual */ uint32_t BluetoothControl::Unregister(IBluetooth::INotification* notification)
    {
        _adminLock.Lock();

        std::list<IBluetooth::INotification*>::iterator index(std::find(_observers.begin(), _observers.end(), notification));

        // Make sure you do not unregister something you did not register !!!
        ASSERT(index != _observers.end());

        if (index != _observers.end()) {
            (*index)->Release();
            _observers.erase(index);
        }

        _adminLock.Unlock();

        return (Core::ERROR_NONE);
    }
    /* virtual */ bool BluetoothControl::Scan(const bool enable)
    {
        if ((_application.IsScanning() == false) && (enable == true)) {

            TRACE(Trace::Information, ("Start Bluetooth Scan"));

            // Clearing previously discovered devices.
            RemoveDevices([](DeviceImpl* device) -> bool { if ((device->IsPaired() == false) && (device->IsConnected() == false)) device->Clear(); return(false); });

            bool lowEnergy = true;
            bool limited = false;
            bool passive = false;
            uint16_t duration = 10;
            uint8_t flags = 0;
            uint32_t type = 0x338B9E;

            if (lowEnergy == true) {
                _application.Scan(duration, limited, passive);
            } else {
                _application.Scan(duration, type, flags);
            }
        } else if ((_application.IsScanning() == true) && (enable == false)) {

            TRACE(Trace::Information, ("Stop Bluetooth Scan"));

            _application.Abort();
        }

        return (_application.IsScanning() == enable);
    }

    /* virtual */ Exchange::IBluetooth::IDevice* BluetoothControl::Device(const string& address)
    {
        IBluetooth::IDevice* result = Find(address);
        if (result != nullptr) {
            result->AddRef();
        }
        return (result);
    }

    /* virtual */ Exchange::IBluetooth::IDevice::IIterator* BluetoothControl::Devices()
    {
        return (Core::Service<DeviceImpl::IteratorImpl>::Create<IBluetooth::IDevice::IIterator>(_devices));
    }

    /* virtual */ void BluetoothControl::Update(const hci_event_hdr& eventData)
    {
        const uint8_t* data = &(reinterpret_cast<const uint8_t*>(&eventData)[sizeof(hci_event_hdr&)]);

        switch (eventData.evt) {
            case EVT_CMD_STATUS: {
                 const evt_cmd_status* cs = reinterpret_cast<const evt_cmd_status*>(data);
                 TRACE(Trace::Information, (_T("==EVT_CMD_STATUS: %X-%03X status: %d"), (((cs->opcode >> 10) & 0xF), (cs->opcode & 0x3FF), cs->status)));
                 break;
            }
            case EVT_CMD_COMPLETE: {
                 const evt_cmd_complete* cc = reinterpret_cast<const evt_cmd_complete*>(data);
                 TRACE(Trace::Information, (_T("==EVT_CMD_COMPLETE: %X-%03X"), (((cc->opcode >> 10) & 0xF), (cc->opcode & 0x3FF))));
                 break;
            }
            case EVT_LE_META_EVENT: {
                 const evt_le_meta_event* eventMetaData = reinterpret_cast<const evt_le_meta_event*>(data);

                 if (eventMetaData->subevent == EVT_LE_CONN_COMPLETE) {
                     TRACE(Trace::Information, (_T("==EVT_LE_CONN_COMPLETE: unexpected")));
                 } else if (eventMetaData->subevent == EVT_LE_READ_REMOTE_USED_FEATURES_COMPLETE) {
                     TRACE(Trace::Information, (_T("==EVT_LE_READ_REMOTE_USED_FEATURES_COMPLETE: unexpected")));
                 } else if (eventMetaData->subevent == EVT_DISCONNECT_PHYSICAL_LINK_COMPLETE) {
                     TRACE(Trace::Information, (_T("==EVT_DISCONNECT_PHYSICAL_LINK_COMPLETE: unexpected")));
                 } else {
                     ASSERT (eventMetaData->subevent != EVT_LE_ADVERTISING_REPORT);
                     TRACE(Trace::Information, (_T("==EVT_LE_META_EVENT: unexpected subevent: %d"), eventMetaData->subevent));
                 }
                 break;
            }
            case 0:
                break;
            default:
                TRACE(Trace::Information, (_T("==UNKNOWN: event %X"), eventData.evt));
                break;
        }
    }    
    void BluetoothControl::Discovered(const bool lowEnergy, const Bluetooth::Address& address, const string& name)
    {
        _adminLock.Lock();

        std::list<DeviceImpl*>::iterator index = _devices.begin();

        while ((index != _devices.end()) && (*(*index) != address)) {
            index++;
        }

        if (index != _devices.end()) {
            (*index)->Discovered();
        } else if (lowEnergy == true) {
            TRACE(Trace::Information, ("Added LowEnergy Bluetooth device: %s, name: %s", address.ToString().c_str(), name.c_str()));
            _devices.push_back(Core::Service<DeviceLowEnergy>::Create<DeviceImpl>(_btInterface, address, name));
        } else {
            TRACE(Trace::Information, ("Added Regular Bluetooth device: %s, name: %s", address.ToString().c_str(), name.c_str()));
            _devices.push_back(Core::Service<DeviceRegular>::Create<DeviceImpl>(_btInterface, address, name));
        }

        _adminLock.Unlock();
    }

    void BluetoothControl::RemoveDevices(std::function<bool(DeviceImpl*)> filter)
    {
        _adminLock.Lock();

        for (std::list<DeviceImpl*>::iterator index = _devices.begin(), end = _devices.end(); index != end; ++index) {
            // call the function passed into findMatchingAddresses and see if it matches
            if (filter(*index) == true) {
                (*index)->Release();
                index = _devices.erase(index);
            }
        }

        _adminLock.Unlock();
    }
    BluetoothControl::DeviceImpl* BluetoothControl::Find(const string& address)
    {
        Bluetooth::Address search(address.c_str());
        std::list<DeviceImpl*>::const_iterator index = _devices.begin();

        while ((index != _devices.end()) && ((*index)->operator==(search) == false)) {
            index++;
        }

        return (index != _devices.end() ? (*index) : nullptr);
    }

}
}
