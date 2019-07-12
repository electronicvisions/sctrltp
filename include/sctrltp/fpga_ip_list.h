#pragma once
#include <string>
#include <vector>

namespace sctrltp {

/**
 * Get list of FPGA IPs available via SLURM_FPGA_IPS in the environment.
 * @return Vector of FPGA IPs as strings
 */
std::vector<std::string> get_fpga_ip_list();

} // namespace sctrltp
