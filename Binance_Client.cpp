
#include "CryptoExtensions.h"
#include "Websocket_Client.cpp" // because of templates

// Client definitions


template<typename T>
Client<T>::Client() : _public_client{ 1 }, flush_params{ 0 }
{};

template<typename T>
Client<T>::Client(std::string key, std::string secret) : _public_client{ 0 }, _api_key{ key }, _api_secret{ secret }, flush_params{ 0 }
{};

// Client CRTP methods
template<typename T>
unsigned long long Client<T>::exchange_time() { return static_cast<T*>(this)->v_exchange_time(); }

template<typename T>
bool Client<T>::ping_client() { return static_cast<T*>(this)->v_ping_client(); }

template<typename T>
bool Client<T>::init_ws_session() { return static_cast<T*>(this)->v_init_ws_session(); }

template<typename T>
std::string Client<T>::_get_listen_key() { return static_cast<T*>(this)->v__get_listen_key(); }

template<typename T>
void Client<T>::close_stream(const std::string& symbol, const std::string& stream_name) { static_cast<T*>(this)->v_close_stream(symbol, stream_name); }

template<typename T>
bool Client<T>::is_stream_open(const std::string& symbol, const std::string& stream_name) { return static_cast<T*>(this)->v_is_stream_open(symbol, stream_name); }

template<typename T>
std::vector<std::string> Client<T>::get_open_streams() { return static_cast<T*>(this)->v_get_open_streams(); }

template<typename T>
void Client<T>::ws_auto_reconnect(const bool& reconnect) { static_cast<T*>(this)->v_ws_auto_reconnect(reconnect); }

template<typename T>
void Client<T>::set_refresh_key_interval(const bool val) { static_cast<T*>(this)->v_set_refresh_key_interval(val); }

// Client other methods

template <typename T>
bool Client<T>::init_rest_session() // make separate for ws and rest
{
	try
	{
		if (this->_rest_client) delete this->_rest_client;

		this->_rest_client = new RestSession{};
		if (!this->_public_client)
		{
			this->set_headers(this->_rest_client);
		}
		if (!(this->ping_client())) return 0;

		return 1;
	}
	catch (...)
	{
		delete this->_rest_client;
		throw("bad_init_rest");
	}

}

template <typename T>
bool Client<T>::set_headers(RestSession* rest_client)
{
	std::string key_header = "X-MBX-APIKEY:" + this->_api_key;
	struct curl_slist* auth_headers;
	auth_headers = curl_slist_append(NULL, key_header.c_str());

	curl_easy_setopt((rest_client->_get_handle), CURLOPT_HTTPHEADER, auth_headers);
	curl_easy_setopt((rest_client->_post_handle), CURLOPT_HTTPHEADER, auth_headers);
	curl_easy_setopt((rest_client->_put_handle), CURLOPT_HTTPHEADER, auth_headers);

	return 0;
}

template<typename T>
std::string Client<T>::_generate_query(Params& params_obj)
{
	std::unordered_map<std::string, std::string> params = params_obj.param_map;
	std::string query;

	for (std::unordered_map<std::string, std::string>::iterator itr = params.begin();
		itr != params.end();
		itr++)
	{
		if (itr != params.begin()) query += "&";

		query += (itr->first + "=" + itr->second);
	}
	return query;
}

template <typename T>
Client<T>::~Client()
{
	delete _rest_client;
	delete _ws_client;
};


// SpotClient definitions

SpotClient::SpotClient() : Client()
{
	this->init_ws_session();
	this->init_rest_session();
};

SpotClient::SpotClient(std::string key, std::string secret)
	: Client(key, secret)
{
	this->init_rest_session();
	this->init_ws_session();
}

unsigned long long SpotClient::v_exchange_time()
{
	std::string full_path = this->_BASE_REST_SPOT + "/api/v3/time";
	std::string ex_time = (this->_rest_client)->_getreq(full_path)["response"]["serverTime"].asString();

	return std::atoll(ex_time.c_str());
}

bool SpotClient::v_ping_client()
{
	try
	{
		std::string full_path = this->_BASE_REST_SPOT + "/api/v3/ping";
		Json::Value ping_response = (this->_rest_client)->_getreq(full_path)["response"];
		return (ping_response != Json::nullValue);
	}
	catch (...)
	{
		throw("bad_ping");
	}
}

bool SpotClient::v_init_ws_session()
{
	try
	{
		if (this->_ws_client) delete this->_ws_client;
		this->_ws_client = new WebsocketClient{ this->_WS_BASE_SPOT, this->_WS_PORT };
		return 1;
	}
	catch (...)
	{
		throw("bad_init_ws");
	}
}

std::string SpotClient::v__get_listen_key()
{
	// no signature is needed here
	std::string full_path = this->_BASE_REST_SPOT + "/api/v3/userDataStream";
	Json::Value response = (this->_rest_client)->_postreq(full_path);

	return response["response"]["listenKey"].asString();
}


void SpotClient::v_close_stream(const std::string& symbol, const std::string& stream_name)
{
	try
	{
		this->_ws_client->close_stream(symbol + "@" + stream_name);
	}
	catch (...)
	{
		throw("stream_close_exc");
	}
}

Json::Value SpotClient::send_order(Params& param_obj)
{

	std::string full_path = this->_BASE_REST_SPOT + "/api/v3/order";
	param_obj.set_param<unsigned long long>("timestamp", local_timestamp());
	std::string query = Client::_generate_query(param_obj);

	std::string signature = HMACsha256(query, this->_api_secret);
	query += ("&signature=" + signature);
	query = "?" + query;

	Json::Value response = (this->_rest_client)->_postreq(full_path + query);

	if (this->flush_params) param_obj.clear_params();

	return response;

}

template <class FT>
unsigned int SpotClient::aggTrade(std::string symbol, std::string& buffer, FT& functor)
{
	// note: symbol must be lowercase. don't add due to reduced performance (reconnect faster during bad times)
	std::string full_stream_name = symbol + '@' + "aggTrade";
	if (this->_ws_client->is_open(full_stream_name))
	{
		std::cout << "already exists";
		return 0;
	}
	else
	{
		this->_ws_client->_stream_manager<FT>(full_stream_name, buffer, functor);
		return this->_ws_client->running_streams[full_stream_name];
	}
}

template <class FT>
unsigned int SpotClient::userStream(std::string& buffer, FT& functor)
{
	RestSession* keep_alive_session = new RestSession{};
	try
	{
		this->set_headers(keep_alive_session);
		std::string full_stream_name = this->_get_listen_key();

		std::string renew_key_path = this->_BASE_REST_SPOT + "/api/v3/userDataStream" + "?" + "listenKey=" + full_stream_name;

		std::pair<RestSession*, std::string> user_stream_pair = std::make_pair(keep_alive_session, renew_key_path);

		if (this->_ws_client->is_open(full_stream_name))
		{
			std::cout << "already exists";
			return 0;
		}
		else
		{
			this->_ws_client->_stream_manager<FT>(full_stream_name, buffer, functor, user_stream_pair);
			return this->_ws_client->running_streams[full_stream_name];
		}
	}
	catch (...)
	{
		delete keep_alive_session;
		throw("bad_stream");
	}
}



void SpotClient::v_set_refresh_key_interval(const bool val)
{
	this->_ws_client->refresh_listenkey_interval = val;
}

bool SpotClient::v_is_stream_open(const std::string& symbol, const std::string& stream_name)
{
	std::string full_stream_name = symbol + '@' + stream_name;
	return this->_ws_client->is_open(full_stream_name);
}

std::vector<std::string> SpotClient::v_get_open_streams()
{
	return this->_ws_client->open_streams();
}

void SpotClient::v_ws_auto_reconnect(const bool& reconnect)
{
	this->_ws_client->_set_reconnect(reconnect);
}

SpotClient::~SpotClient()
{
	delete _rest_client;
	delete _ws_client;
};

// FuturesClient definitions

FuturesClient::FuturesClient()
	: Client()
{
	this->init_ws_session();
	this->init_rest_session();
};

FuturesClient::FuturesClient(std::string key, std::string secret)
	: Client(key, secret)
{
	this->init_rest_session();
	this->init_ws_session();
}

unsigned long long FuturesClient::v_exchange_time()
{
	std::string full_path = this->_BASE_REST_FUTURES + "/fapi/v1/time"; // fix
	std::string ex_time = (this->_rest_client)->_getreq(full_path)["response"]["serverTime"].asString();

	return std::atoll(ex_time.c_str());
}

bool FuturesClient::v_ping_client()
{
	try
	{
		std::string full_path = this->_BASE_REST_FUTURES + "/fapi/v1/ping";
		Json::Value ping_response = (this->_rest_client)->_getreq(full_path)["response"];
		return (ping_response != Json::nullValue);
	}
	catch (...)
	{
		throw("bad_ping");
	}
}


bool FuturesClient::v_init_ws_session()
{
	try
	{
		if (this->_ws_client) delete this->_ws_client;
		this->_ws_client = new WebsocketClient{ this->_WS_BASE_FUTURES, this->_WS_PORT };
		return 1;
	}
	catch (...)
	{
		throw("bad_init_ws");
	}
}

std::string FuturesClient::v__get_listen_key()
{
	std::string full_path = this->_BASE_REST_FUTURES + "/fapi/v1/listenKey";
	Params temp_params;
	temp_params.set_param<unsigned long long>("timestamp", local_timestamp());
	std::string query = Client::_generate_query(temp_params);

	std::string signature = HMACsha256(query, this->_api_secret);
	query += ("&signature=" + signature);
	query = "?" + query;

	Json::Value response = (this->_rest_client)->_postreq(full_path + query);

	return response["response"]["listenKey"].asString();
}

template <class FT>
unsigned int FuturesClient::userStream(std::string& buffer, FT& functor)
{
	RestSession* keep_alive_session = new RestSession{ this->_api_key, this->_api_secret };

	try
	{
		this->set_headers(keep_alive_session);

		std::string renew_key_path = this->_BASE_REST_FUTURES + "/fapi/v1/listenKey";

		std::pair<RestSession*, std::string> user_stream_pair = std::make_pair(keep_alive_session, renew_key_path);

		std::string full_stream_name = this->_get_listen_key();
		if (this->_ws_client->is_open(full_stream_name))
		{
			std::cout << "already exists";
			return 0;
		}
		else
		{
			this->_ws_client->_stream_manager<FT>(full_stream_name, buffer, functor);
			return this->_ws_client->running_streams[full_stream_name];
		}
	}
	catch (...)
	{
		delete keep_alive_session;
		throw("bad_ws_stream");
	}
}

void FuturesClient::v_close_stream(const std::string& symbol, const std::string& stream_name)
{
	try
	{
		this->_ws_client->close_stream(symbol + "@" + stream_name);
	}
	catch (...)
	{
		throw("stream_close_exc");
	}
}

std::vector<std::string> FuturesClient::v_get_open_streams()
{
	return this->_ws_client->open_streams();
}

Json::Value FuturesClient::send_order(Params& param_obj)
{
	std::string full_path = this->_BASE_REST_FUTURES + "/fapi/v1/order";
	param_obj.set_param<unsigned long long>("timestamp", local_timestamp());
	std::string query = Client::_generate_query(param_obj);

	std::string signature = HMACsha256(query, this->_api_secret);
	query += ("&signature=" + signature);
	query = "?" + query;

	Json::Value response = (this->_rest_client)->_postreq(full_path + query); // return entire json?

	if (this->flush_params) param_obj.clear_params();

	return response;
}

Json::Value FuturesClient::fetch_balances(Params& param_obj)
{
	std::string full_path = this->_BASE_REST_FUTURES + "/fapi/v2/balance";

	param_obj.set_param<unsigned long long>("timestamp", local_timestamp());
	std::string query = Client::_generate_query(param_obj);

	std::string signature = HMACsha256(query, this->_api_secret);
	query += ("&signature=" + signature);
	query = "?" + query;

	Json::Value response = (this->_rest_client)->_getreq(full_path + query);

	if (this->flush_params) param_obj.clear_params();

	return response;
}


unsigned int FuturesClient::aggTrade(std::string symbol)
{
	return 0;
}


void FuturesClient::v_set_refresh_key_interval(const bool val)
{
	this->_ws_client->refresh_listenkey_interval = val;
}

bool FuturesClient::v_is_stream_open(const std::string& symbol, const std::string& stream_name)
{
	std::string full_stream_name = symbol + '@' + stream_name;
	return this->_ws_client->is_open(full_stream_name);
}

void FuturesClient::v_ws_auto_reconnect(const bool& reconnect)
{
	this->_ws_client->_set_reconnect(reconnect);
}

FuturesClient::~FuturesClient()
{
	delete _rest_client;
	delete _ws_client;
}

// Params definitions

Params::Params() {};

Params::Params(Params& params_obj)
{
	this->param_map = params_obj.param_map;
}

Params::Params(const Params& params_obj)
{
	this->param_map = params_obj.param_map;
}

Params& Params::operator=(Params& params_obj)
{
	this->param_map = params_obj.param_map;

	return *this;
}

Params& Params::operator=(const Params& params_obj)
{
	this->param_map = params_obj.param_map;

	return *this;
}

template <typename PT>
void Params::set_param(std::string key, PT value)
{
	param_map[key] = std::to_string(value);
}
template <> // do not call to_string on a string
void Params::set_param<std::string>(std::string key, std::string value)
{
	param_map[key] = value;
}

bool Params::clear_params()
{
	this->param_map.clear();
	return this->empty();
}

bool Params::empty()
{
	return this->param_map.empty();
}