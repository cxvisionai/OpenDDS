#include "PubDriver.h"
#include "TestException.h"
// Add the TransportImpl.h before TransportImpl_rch.h is included to
// resolve the build problem that the class is not defined when
// RcHandle<T> template is instantiated.
#include "dds/DCPS/transport/framework/TransportImpl.h"
#include "dds/DCPS/transport/simpleUnreliableDgram/SimpleMcastConfiguration.h"
#include "dds/DCPS/transport/framework/TheTransportFactory.h"
#include "dds/DCPS/transport/framework/NetworkAddress.h"
#include "dds/DCPS/AssociationData.h"
#include "dds/DCPS/RepoIdBuilder.h"
#include "dds/DCPS/Service_Participant.h"
#include "SimplePublisher.h"
#include <ace/Arg_Shifter.h>
#include <ace/Argv_Type_Converter.h>

#include "dds/DCPS/transport/framework/EntryExit.h"


PubDriver::PubDriver()
: pub_id_ (OpenDDS::DCPS::GuidBuilder::create ()),
  sub_id_ (OpenDDS::DCPS::GuidBuilder::create ())
{
  DBG_ENTRY("PubDriver","PubDriver");
}


PubDriver::~PubDriver()
{
  DBG_ENTRY("PubDriver","~PubDriver");
}


void
PubDriver::run(int& argc, ACE_TCHAR* argv[])
{
  DBG_ENTRY_LVL("PubDriver","run",6);

  // Need call the ORB_init to dynamically load the SimpleMcast library via
  // service configurator.
  // initialize the orb
  ACE_Argv_Type_Converter conv(argc, argv);
  CORBA::ORB_var orb = CORBA::ORB_init (conv.get_argc(),
                                        conv.get_ASCII_argv(),
                                        "TAO_DDS_DCPS");

  TheServiceParticipant->set_ORB (orb.in());
  DDS::DomainParticipantFactory_var dpf;
  dpf = TheParticipantFactoryWithArgs(argc, argv);

  this->parse_args(argc, argv);
  this->init();
  this->run();
}


void
PubDriver::parse_args(int& argc, ACE_TCHAR* argv[])
{
  DBG_ENTRY("PubDriver","parse_args");

  // Command-line arguments:
  //
  // -p <pub_id:pub_host:pub_port>
  // -s <sub_id:if_addr>
  //
  ACE_Arg_Shifter arg_shifter(argc, argv);

  bool got_p = false;
  bool got_s = false;

  const ACE_TCHAR* current_arg = 0;

  while (arg_shifter.is_anything_left())
  {
    // The '-p' option
    if ((current_arg = arg_shifter.get_the_parameter(ACE_TEXT("-p")))) {
      if (got_p) {
        ACE_ERROR((LM_ERROR,
                   "(%P|%t) Only one -p allowed on command-line.\n"));
        throw TestException();
      }

      int result = parse_pub_arg(current_arg);
      arg_shifter.consume_arg();

      if (result != 0) {
        ACE_ERROR((LM_ERROR,
                   "(%P|%t) Failed to parse -p command-line arg.\n"));
        throw TestException();
      }

      got_p = true;
    }
    // A '-s' option
    else if ((current_arg = arg_shifter.get_the_parameter(ACE_TEXT("-s")))) {
      if (got_s) {
        ACE_ERROR((LM_ERROR,
                   "(%P|%t) Only one -s allowed on command-line.\n"));
        throw TestException();
      }

      int result = parse_sub_arg(current_arg);
      arg_shifter.consume_arg();

      if (result != 0) {
        ACE_ERROR((LM_ERROR,
                   "(%P|%t) Failed to parse -s command-line arg.\n"));
        throw TestException();
      }

      got_s = true;
    }
    // The '-?' option
    else if (arg_shifter.cur_arg_strncasecmp(ACE_TEXT("-?")) == 0) {
      ACE_DEBUG((LM_DEBUG,
                 "usage: %s "
                 "-p pub_id:pub_host:pub_port -s sub_id:if_addr\n",
                 argv[0]));

      arg_shifter.consume_arg();
      throw TestException();
    }
    // Anything else we just skip
    else {
      arg_shifter.ignore_arg();
    }
  }

  // Make sure we got the required arguments:
  if (!got_p) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) -p command-line option not specified (required).\n"));
    throw TestException();
  }

  if (!got_s) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) -s command-line option not specified (required).\n"));
    throw TestException();
  }
}


void
PubDriver::init()
{
  DBG_ENTRY("PubDriver","init");

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Use TheTransportFactory to create a TransportImpl object "
             "of SimpleMcast with the ALL_TRAFFIC transport_id (%d).\n",
             ALL_TRAFFIC));

  OpenDDS::DCPS::TransportImpl_rch transport_impl
    = TheTransportFactory->create_transport_impl (ALL_TRAFFIC,
                                                  ACE_TEXT("SimpleMcast"),
                                                  OpenDDS::DCPS::DONT_AUTO_CONFIG);

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Get the existing or create a new SimpleMcastConfiguration object.\n"));

  OpenDDS::DCPS::TransportConfiguration_rch config
    = TheTransportFactory->create_configuration (ALL_TRAFFIC, ACE_TEXT("SimpleMcast"));

  OpenDDS::DCPS::SimpleMcastConfiguration* mcast_config
    = static_cast <OpenDDS::DCPS::SimpleMcastConfiguration*> (config.in ());

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Set the config->local_address_ to our (local) pub_addr_.\n"));

  //mcast_config->local_address_ = this->if_addr_;
  mcast_config->multicast_group_address_ = this->pub_addr_;
  mcast_config->multicast_group_address_str_ = this->pub_addr_str_;


  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Configure the (ALL_TRAFFIC) TransportImpl object.\n"));

  // Pass the configuration object to the transport_impl's configure()
  // method as an "in" argument.  Don't change the configuration object
  // after this has been done since it will cause unknown results.
  if (transport_impl->configure(config.in()) != 0)
    {
      ACE_ERROR((LM_ERROR,
                 "(%P|%t) Failed to configure the transport impl\n"));
      throw TestException();
    }

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "The TransportImpl object has been successfully configured.\n"));
}


void
PubDriver::run()
{
  DBG_ENTRY_LVL("PubDriver","run",6);

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Create the 'subscriptions' (array of AssociationData).\n"));

  // Set up the subscriptions.
  OpenDDS::DCPS::AssociationData subscriptions[1];
  subscriptions[0].remote_id_                = this->sub_id_;
  subscriptions[0].remote_data_.transport_id = 3;  // TBD - not right
  subscriptions[0].remote_data_.publication_transport_priority = 0;

  OpenDDS::DCPS::NetworkAddress network_order_address(this->pub_addr_str_);

  ACE_OutputCDR cdr;
  cdr << network_order_address;
  size_t len = cdr.total_length ();

  subscriptions[0].remote_data_.data
    = OpenDDS::DCPS::TransportInterfaceBLOB
    (len,
    len,
    (CORBA::Octet*)(cdr.buffer ()));

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Initialize our SimplePublisher object.\n"));

  this->publisher_.init(ALL_TRAFFIC,
                        this->pub_id_,
                        1,               /* size of subscriptions array */
                        subscriptions);

  // Wait for a fully association establishment and then start sending samples.
  ACE_OS::sleep (2);

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Run our SimplePublisher object.\n"));

  this->publisher_.run();

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "Ask the SimplePublisher object if it is done running. "
             "If not, sleep for 1 second, and ask again.\n"));

  while (this->publisher_.delivered_test_message() == 0)
    {
      ACE_OS::sleep(1);
    }

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "The SimplePublisher object is done running.  "
             "Release TheTransportFactory - causing all TransportImpl "
             "objects to be shutdown().\n"));

  // Tear-down the entire Transport Framework.
  TheTransportFactory->release();
  TheServiceParticipant->shutdown();

  VDBG((LM_DEBUG, "(%P|%t) DBG:   "
             "TheTransportFactory has finished releasing.\n"));
}


int
PubDriver::parse_pub_arg(const ACE_TString &arg)
{
  DBG_ENTRY("PubDriver","parse_pub_arg");

  ACE_TString::size_type pos;

  // Find the first ':' character, and make sure it is in a legal spot.
  if ((pos = arg.find(ACE_TEXT(':'))) == ACE_TString::npos) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) Bad -p command-line value (%s). Missing ':' char.\n",
               arg.c_str()));
    return -1;
  }

  if (pos == 0) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) Bad -p command-line value (%s). "
               "':' char cannot be first char.\n",
               arg.c_str()));
    return -1;
  }

  if (pos == (arg.length() - 1)) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) Bad -p command-line value  (%s) - "
               "':' char cannot be last char.\n",
               arg.c_str()));
    return -1;
  }

  // Parse the pub_id from left of ':' char, and remainder to right of ':'.
  ACE_TString pub_id_str(arg.c_str(), pos);
  this->pub_addr_str_ = arg.c_str() + pos + 1;
  
  // RepoIds are conventionally created and managed by the DCPSInfoRepo. Those
  // generated here are for the sole purpose of verifying internal behavior.
  OpenDDS::DCPS::RepoIdBuilder builder(pub_id_);

  builder.participantId(1);
  builder.entityKey(ACE_OS::atoi(pub_id_str.c_str()));
  builder.entityKind(OpenDDS::DCPS::ENTITYKIND_USER_WRITER_WITH_KEY);
  
  this->pub_addr_ = ACE_INET_Addr(this->pub_addr_str_.c_str());

  return 0;
}


int
PubDriver::parse_sub_arg(const ACE_TString& arg)
{
  DBG_ENTRY("PubDriver","parse_sub_arg");

  ACE_TString::size_type pos;

  // Find the first ':' character, and make sure it is in a legal spot.
  if ((pos = arg.find(ACE_TEXT(':'))) == ACE_TString::npos) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) Bad -s command-line value (%s). Missing ':' char.\n",
               arg.c_str()));
    return -1;
  }

  if (pos == 0) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) Bad -s command-line value (%s). "
               "':' char cannot be first char.\n",
               arg.c_str()));
    return -1;
  }

  if (pos == (arg.length() - 1)) {
    ACE_ERROR((LM_ERROR,
               "(%P|%t) Bad -s command-line value  (%s) - "
               "':' char cannot be last char.\n",
               arg.c_str()));
    return -1;
  }

  // Parse the sub_id from left of ':' char, and remainder to right of ':'.
  ACE_TString sub_id_str(arg.c_str(), pos);
  ACE_TString if_addr_str(arg.c_str() + pos + 1);
  
  // RepoIds are conventionally created and managed by the DCPSInfoRepo. Those
  // generated here are for the sole purpose of verifying internal behavior.
  OpenDDS::DCPS::RepoIdBuilder builder(sub_id_);

  builder.participantId(1);
  builder.entityKey(ACE_OS::atoi(sub_id_str.c_str()));
  builder.entityKind(OpenDDS::DCPS::ENTITYKIND_USER_WRITER_WITH_KEY);

  // Use the remainder as the "stringified" ACE_INET_Addr.
  // Add in a dummy port # so ACE_INET_Addr can parse the address
  if_addr_str += ACE_TEXT(":0");
  this->if_addr_ = ACE_INET_Addr(if_addr_str.c_str());

  return 0;
}
