#include "sctrltp/fpga_ip_list.h"

#include <sstream>

namespace sctrltp {

std::vector<std::string> get_fpga_ip_list()
{
	char const* env_ip_list = std::getenv("SLURM_FPGA_IPS");
	std::vector<std::string> ip_list;
	std::istringstream env_ip_list_stream(env_ip_list);
	std::string ip;
	while (getline(env_ip_list_stream, ip, ',')) {
		ip_list.push_back(ip);
	}

	return ip_list;
}

} // namespace sctrltp
