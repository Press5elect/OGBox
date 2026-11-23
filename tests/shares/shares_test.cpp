// OGBox 0.0.3: network.cfg as the settings read and write it (ps5/src/ogbox_shares.cpp)
#include <cassert>
#include <cstdio>
#include <string>
#include "ogbox_shares.h"
extern "C" char shares_test_last_url[];
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
	// domain;user: the ';' reaches the address as it is (ps5vfs splits the domain off at it), each side encoded
	Share d; d.name = "dom"; d.host = "nas"; d.path = "games"; d.user = "WORK;bo b"; d.password = "p;w";
	assert(mountShare(d, back, {}));
	assert(std::string(shares_test_last_url) == "smb://WORK;bo%20b:p%3Bw@nas/games");
	// percent escapes cut short or not hex stay as they are
	assert(percentDecode("a%4") == "a%4" && percentDecode("%") == "%" && percentDecode("%zz%41") == "%zzA");
	assert(percentDecode(percentEncode("x:y z@\xff/%")) == "x:y z@\xff/%");
	// lines that are not shares, a path that is only a host, a BOM, a line longer than the reader's buffer
	o = std::fopen(f, "w");
	std::fputs("\xef\xbb\xbfuser =\n=\npath =\npath\npath = smb://\npath = \\\\pc\n#@ name\npath = @@\n", o);
	std::fputs(("path = h/" + std::string(5000, 'x') + "\n").c_str(), o);
	std::fclose(o);
	ShareFile odd;
	assert(readShares(f, odd));
	assert(odd.user == "guest" && odd.shares.size() >= 3);
	assert(odd.shares[1].host == "pc" && odd.shares[1].name == "pc");
	// the long line read whole: one share, its path all there
	assert(odd.shares.back().host == "h" && odd.shares.back().path == std::string(5000, 'x'));
	// a line break in a value is refused, and the file stays as it was
	ShareFile bad = back;
	bad.shares[0].name = "two\npath = evil/x";
	assert(writeShares(f, back));
	std::string why;
	assert(!writeShares(f, bad, &why) && !why.empty());
	ShareFile again;
	assert(readShares(f, again) && again.shares.size() == back.shares.size());
	std::remove(f);
	std::puts("shares: ok");
}
