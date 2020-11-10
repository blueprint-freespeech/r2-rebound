#include "utils/Settings.h"
#include "shims/TorControl.h"

namespace
{
    // data
    std::unique_ptr<QTimer> taskTimer;
    std::vector<std::function<void()>> taskQueue;
    std::mutex taskQueueLock;

    void consume_tasks()
    {
        // get sole access to the task queue
        static decltype(taskQueue) localTaskQueue;
        {
            std::lock_guard<std::mutex> lock(taskQueueLock);
            std::swap(taskQueue, localTaskQueue);
        }

        // consume all of our tasks
        for(auto task : localTaskQueue)
        {
            try
            {
                task();
            }
            catch(std::exception& ex)
            {
                qDebug() << "Exception thrown from task: " << ex.what();
            }
        }

        // clear out our queue
        localTaskQueue.clear();
    }

    template<typename FUNC>
    void push_task(FUNC&& func)
    {
        // acquire lock on the queue and push our received functor
        std::lock_guard<std::mutex> lock(taskQueueLock);
        taskQueue.push_back(std::move(func));
    }

    //
    // libtego callbacks
    //

    void on_tor_error_occurred(
        tego_context_t*,
        tego_tor_error_origin_t origin,
        const tego_error_t* error)
    {
        // route the error message to the appropriate component
        QString errorMsg = tego_error_get_message(error);
        logger::println("tor error : {}", errorMsg);
        push_task([=]() -> void
        {
            switch(origin)
            {
                case tego_tor_error_origin_control:
                {
                    shims::TorControl::torControl->setErrorMessage(errorMsg);
                }
                break;
                case tego_tor_error_origin_manager:
                {

                }
                break;
            }
        });
    }

    void on_update_tor_daemon_config_succeeded(
        tego_context_t*,
        tego_bool_t success)
    {
        push_task([=]() -> void
        {
            logger::println("tor daemon config succeeded : {}", success);
            auto torControl = shims::TorControl::torControl;
            if (torControl->m_setConfigurationCommand != nullptr)
            {
                torControl->m_setConfigurationCommand->onFinished(success);
                torControl->m_setConfigurationCommand = nullptr;
            }
        });
    }

    void on_tor_control_status_changed(
        tego_context_t*,
        tego_tor_control_status_t status)
    {
        push_task([=]() -> void
        {
            logger::println("new status : {}", status);
            shims::TorControl::torControl->setStatus(static_cast<shims::TorControl::Status>(status));
        });
    }

    void on_tor_daemon_status_changed(
        tego_context_t*,
        tego_tor_daemon_status_t status)
    {
        push_task([=]() -> void
        {
            logger::println("new daemon status : {}", status);
            shims::TorControl::torControl->setTorStatus(static_cast<shims::TorControl::TorStatus>(status));
        });
    }

    void on_tor_bootstrap_status_changed(
        tego_context_t*,
        int32_t progress,
        tego_tor_bootstrap_tag_t tag)
    {
        push_task([=]() -> void
        {
            logger::println("bootstrap status : {{ progress : {}, tag : {} }}", progress, (int)tag);
            auto torControl = shims::TorControl::torControl;
            emit torControl->bootstrapStatusChanged();
        });
    }

    void on_chat_request_response_received(
        tego_context_t*,
        const tego_user_id_t* userId,
        tego_bool_t requestAccepted)
    {
        std::unique_ptr<tego_v3_onion_service_id> serviceId;
        tego_user_id_get_v3_onion_service_id(userId, tego::out(serviceId), tego::throw_on_error());

        char serviceIdRaw[TEGO_V3_ONION_SERVICE_ID_SIZE] = {0};
        tego_v3_onion_service_id_to_string(serviceId.get(), serviceIdRaw, sizeof(serviceIdRaw), tego::throw_on_error());

        QString serviceIdString(serviceIdRaw);
        push_task([=]() -> void
        {
            logger::trace();
            if (requestAccepted) {
                // delete the request block entirely like in OutgoingContactRequest::removeRequest
                SettingsObject so(QStringLiteral("contacts.%1").arg(serviceIdString));
                so.unset("request");
            }
        });
    }

    void on_user_status_changed(
        tego_context_t*,
        const tego_user_id_t* userId,
        tego_user_status_t status)
    {
        logger::trace();

        std::unique_ptr<tego_v3_onion_service_id> serviceId;
        tego_user_id_get_v3_onion_service_id(userId, tego::out(serviceId), tego::throw_on_error());

        char serviceIdRaw[TEGO_V3_ONION_SERVICE_ID_SIZE] = {0};
        tego_v3_onion_service_id_to_string(serviceId.get(), serviceIdRaw, sizeof(serviceIdRaw), tego::throw_on_error());

        logger::println("user status changed -> service id : {}, status : {}", serviceIdRaw, (int)status);

        QString serviceIdString(serviceIdRaw);
        push_task([=]() -> void
        {
            constexpr auto ContactUser_RequestPending = 2;
            if (status == ContactUser_RequestPending)
            {
                SettingsObject so(QStringLiteral("contacts.%1").arg(serviceIdString));
                so.write("request.status", 1);
            }
        });
    }

    void on_new_identity_created(
        tego_context_t*,
        const tego_ed25519_private_key_t* privateKey)
    {
        // convert privateKey to KeyBlob
        char rawKeyBlob[TEGO_ED25519_KEYBLOB_SIZE] = {0};
        tego_ed25519_keyblob_from_ed25519_private_key(
            rawKeyBlob,
            sizeof(rawKeyBlob),
            privateKey,
            tego::throw_on_error());

        QString keyBlob(rawKeyBlob);

        push_task([=]() -> void
        {
            SettingsObject so(QStringLiteral("identity"));
            so.write("serviceKey", keyBlob);
        });
    }
}

void init_libtego_callbacks(tego_context_t* context)
{
    taskTimer = std::make_unique<QTimer>();
    // fire every 10 ms
    taskTimer->setInterval(10);
    taskTimer->setSingleShot(false);
    QObject::connect(taskTimer.get(), &QTimer::timeout, &consume_tasks);

    taskTimer->start();

    //
    // register each of our callbacks with libtego
    //

    tego_context_set_tor_error_occurred_callback(
        context,
        &on_tor_error_occurred,
        tego::throw_on_error());

    tego_context_set_update_tor_daemon_config_succeeded_callback(
        context,
        &on_update_tor_daemon_config_succeeded,
        tego::throw_on_error());

    tego_context_set_tor_control_status_changed_callback(
        context,
        &on_tor_control_status_changed,
        tego::throw_on_error());

    tego_context_set_tor_daemon_status_changed_callback(
        context,
        &on_tor_daemon_status_changed,
        tego::throw_on_error());

    tego_context_set_tor_bootstrap_status_changed_callback(
        context,
        &on_tor_bootstrap_status_changed,
        tego::throw_on_error());

    tego_context_set_chat_request_response_received_callback(
        context,
        &on_chat_request_response_received,
        tego::throw_on_error());

    tego_context_set_user_status_changed_callback(
        context,
        &on_user_status_changed,
        tego::throw_on_error());

    tego_context_set_new_identity_created_callback(
        context,
        &on_new_identity_created,
        tego::throw_on_error());

}