#include"ns3/core-module.h"
#include"ns3/network-module.h"
#include"ns3/mobility-module.h"
#include"ns3/lte-module.h"
#include"ns3/lte-helper.h"
#include"ns3/epc-helper.h"
#include"ns3/ipv4-global-routing-helper.h"
#include"ns3/config-store.h"
#include"ns3/internet-module.h"
#include"ns3/point-to-point-module.h"
#include"ns3/applications-module.h"
#include"ns3/netanim-module.h"
/*
 * create an LTE simulation with EPC
 */
using namespace ns3;
NS_LOG_COMPONENT_DEFINE("EpcFirstExample");
int main(int argc,char*argv[]){
	uint16_t numOfNode=2;
	double simTime=1.1;
	double distance=300.0;  //设置距离为300m
	double interPacketInterval=150; 
	//加入命令行以方便修改参数
	CommandLine cmd;
	cmd.AddValue("numOfNode","Number of eNodes +UE pairs",numOfNode);
	cmd.AddValue("simTime","Total duration of the simulation [s]",simTime);
	cmd.AddValue("distance","Distance between eNBs [m]",distance);
	cmd.AddValue("interPacketInterval","Inter packet interval [ms]",interPacketInterval);
	cmd.Parse(argc,argv);
	ConfigStore inputConfig;
	inputConfig.ConfigureDefaults();
	cmd.Parse(argc,argv);
	//create a ltehelper object  epc object
	Ptr<LteHelper> lte=CreateObject<LteHelper>();
	Ptr<PointToPointEpcHelper> epc=CreateObject<PointToPointEpcHelper>();
	//告诉ltehelper epc将会使用
	lte->SetEpcHelper(epc);
	//epchelper 自动创建和配置Pgw节点，下一步需要将pgw与其他IPV4网络如Internet连接
	Ptr<Node> pgw=epc->GetPgwNode();
	//创建一个romotehost节点并安装协议栈
	NodeContainer remoteHostContainer;
	remoteHostContainer.Create(1);
	Ptr<Node> remoteHost=remoteHostContainer.Get(0);
	InternetStackHelper internet;
	internet.Install(remoteHostContainer);//创建一个remoteHost,遵循internet协议
	//pgw remoteHost 安装P2P2设备
	PointToPointHelper p2ph;
	p2ph.SetDeviceAttribute("DataRate",DataRateValue(DataRate("100Gb/s"))); //发送数据频率为100 GB/s
	p2ph.SetDeviceAttribute("Mtu",UintegerValue(1500));//最大传输单元
	p2ph.SetChannelAttribute("Delay",TimeValue(Seconds(0.010)));
	NetDeviceContainer internetDevice=p2ph.Install(pgw,remoteHost);
	//pgw remoteHost分配IP地址
	Ipv4AddressHelper ipv4h;
	ipv4h.SetBase("1.0.0.0","255.0.0.0");
	Ipv4InterfaceContainer internetIpIface=ipv4h.Assign(internetDevice);//创建网络
	Ipv4Address remoteHostAddr=internetIpIface.GetAddress(1);//接口0 是localhost 接口1是 p2p device
	//remoteHost 怎么才能路由到Ue，利用UE默认在公共网络7.0.0.0
	Ipv4StaticRoutingHelper ipv4RoutingHelper;
	Ptr<Ipv4StaticRouting> remoteHostStaticRouting=ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
	remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),Ipv4Mask("255.0.0.0"),1);
	 
	//为UE eNB创建节点
	NodeContainer enbNodes;
	enbNodes.Create(numOfNode);
	NodeContainer ueNodes;
	ueNodes.Create(numOfNode);
	//为节点配置移动模型
	Ptr<ListPositionAllocator> positionAlloc=CreateObject<ListPositionAllocator>();
	for (uint16_t i=0;i<numOfNode;i++)
	    positionAlloc->Add(Vector(distance*i,0,0));//初始位置(distance*i,0,0)
	MobilityHelper mobility;
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.SetPositionAllocator(positionAlloc);
	mobility.Install(enbNodes);
	//mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(ueNodes);
	//装载LTE设备及信道
	NetDeviceContainer enbdevice;
	enbdevice=lte->InstallEnbDevice(enbNodes);
	NetDeviceContainer uedevice;
	uedevice=lte->InstallUeDevice(ueNodes);
	 
	//这里不再是LTE-ONLY，需要将LTE连接IP 分配IP地址给UE
	internet.Install(ueNodes);//安装IP协议栈在UE节点
	Ipv4InterfaceContainer ueIpIface;
	ueIpIface=epc->AssignUeIpv4Address(NetDeviceContainer(uedevice));
	//
	for(uint32_t u=0;u<ueNodes.GetN();++u)
	  {
		Ptr<Node> ue=ueNodes.Get(u);
	   	Ptr<Ipv4StaticRouting> ueStaticRouting=ipv4RoutingHelper.GetStaticRouting(ue->GetObject<Ipv4>());//为UE设置默认网关
	   	ueStaticRouting->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(),1);
	  }
	//关联UE 和基站eNB，根据eNB配置来配置每一个UE并创建UE-ENB之间的RRC连接
	for(uint16_t i=0;i<numOfNode;i++)
	  {
	    lte->Attach(uedevice.Get(i),enbdevice.Get(i));//默认EPS承载将激活
	  }
	//含有EPC时ActivateDataRadioBearer不用了
	/*//激活EPS承载包括UE-ENB之间的无线承载
	enum EpsBearer::Qci q=EpsBearer::GBR_CONV_VOICE;
	EpsBearer bearer(q);
	lte->ActivateDataRadioBearer(uedevice,bearer);*/
	//改用默认EPS承载（由LteHelper::Attcah()自动激活）或者专用EPS承载（LteHelper::ActivateDedicatedEpsBearer()）
	Ptr<EpcTft> tft=Create<EpcTft>();
	EpcTft::PacketFilter pf;
	pf.localPortStart=1234;
	pf.localPortEnd=1234;
	tft->Add(pf);
	lte->ActivateDedicatedEpsBearer(uedevice,EpsBearer(EpsBearer::NGBR_VIDEO_TCP_DEFAULT),tft);
	//最后是在UE节点安装应用（与远程应用程序通信）
	//下面是一个UdpClient APP 建立在远程客户端 下行
	uint16_t dl_port=1234;
	uint16_t ul_port=2000;
	uint16_t other_port=3000;
	ApplicationContainer clientApps;
	ApplicationContainer serverApps;
	for(uint32_t u=0;u<ueNodes.GetN();++u){
		++ul_port;++other_port;
	
		PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),dl_port));
		PacketSinkHelper ulPacketSinkHelper("ns3::UdpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),ul_port));
		PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory",InetSocketAddress(Ipv4Address::GetAny(),other_port));
		serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
		serverApps.Add(ulPacketSinkHelper.Install(remoteHost));
		serverApps.Add(packetSinkHelper.Install(ueNodes.Get(u)));
		 
		UdpClientHelper dlClient (ueIpIface.GetAddress(u),dl_port);
		dlClient.SetAttribute("Interval",TimeValue(MilliSeconds(interPacketInterval)));
		dlClient.SetAttribute("MaxPackets",UintegerValue(1000000));
		 
		UdpClientHelper ulClient (remoteHostAddr,ul_port);
		ulClient.SetAttribute("Interval",TimeValue(MilliSeconds(interPacketInterval)));
		ulClient.SetAttribute("MaxPackets",UintegerValue(1000000));
		 
		UdpClientHelper Client (ueIpIface.GetAddress(u),other_port);
		Client.SetAttribute("Interval",TimeValue(MilliSeconds(interPacketInterval)));
		Client.SetAttribute("MaxPackets",UintegerValue(1000000));
		 
		clientApps.Add(dlClient.Install(remoteHost));
		clientApps.Add(ulClient.Install(ueNodes.Get(u)));
	 
		if(u+1<ueNodes.GetN()){
	    		clientApps.Add(Client.Install(ueNodes.Get(u+1)));
		}
		else{
	 	  clientApps.Add(Client.Install(ueNodes.Get(0)));//第一个节点运行
		}
	}
	 
	//设置仿真参数
	serverApps.Start(Seconds(0.01));
	clientApps.Start(Seconds(0.01));
	lte->EnableTraces();
	Simulator::Stop(Seconds(simTime));
	AnimationInterface anim("lte-car.xml");
	Simulator::Run();
	Simulator::Destroy();
	return 0;
}
