// OGBox 0.0.3: network.cfg as the settings read and write it (ps5/src/ogbox_shares.cpp)
#include <cassert>
#include <cstdio>
#include "ogbox_shares.h"
using namespace ogbox;
int main()
{
	const char *f = "/tmp/ogbox-shares-test.cfg";
	FILE *o = std::fopen(f, "w");
	std::fputs("# old\nuser = kuba\npassword = p w\n\npath = 192.168.1.10/Games/Xbox\npath = nfs://nas/volume1/xbox?version=4\n"
		"#@ name = Desk PC\npath = smb://bob:a%40b@desk/share/x\n", o);
	std::fclose(o);
	ShareFile sf;
	assert(readShares(f, sf));
	assert(sf.shares.size() == 3 && sf.user == "kuba" && sf.password == "p w");
	assert(sf.shares[0].name == "Xbox" && sf.shares[0].host == "192.168.1.10" && sf.shares[0].path == "Games/Xbox" && sf.shares[0].user.empty());
	assert(sf.shares[1].nfs && sf.shares[1].host == "nas" && sf.shares[1].path == "/volume1/xbox?version=4");
	assert(shareAddress(sf.shares[1]) == "nfs://nas/volume1/xbox");
	assert(sf.shares[2].name == "Desk PC" && sf.shares[2].user == "bob" && sf.shares[2].password == "a@b");
	Share n; n.name = "Syn ology"; n.host = "10.0.0.2"; n.path = "games"; n.user = "me"; n.password = "x:y z";
	sf.shares.push_back(n);
	assert(writeShares(f, sf));
	ShareFile back;
	assert(readShares(f, back));
	assert(back.shares.size() == 4 && back.shares[3].password == "x:y z" && back.shares[3].name == "Syn ology");
	assert(back.shares[1].path == "/volume1/xbox?version=4" && back.user == "kuba");
	assert(shareMountName("Syn ology", { "/smb/Syn_ology" }) == "Syn_ology-2");
	Share m = back.shares[3];
	assert(mountShare(m, back, {}));
	assert(m.mount == "/smb/Syn_ology");
	std::puts("shares: ok");
}
