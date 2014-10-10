#include "../Hanota.hpp"
#include <iostream>
#include <vector>
#include <sstream>
using namespace std;

struct move
{
    template <typename Iter, typename Pilar>
    void operator () (Iter it, const Pilar& from, const Pilar& to)
    {
	cout << "move " << *it << " from " << from << " to " << to << endl;
    }
};

int main()
{
    cout << "test case 1:" << endl;
    int disc[] = {0, 1, 2};
    int pilar[] = {0, 1, 2};
    hanota(disc, disc + 3, pilar[0], pilar[2], pilar[1], ::move());

    cout << "test case 2:" << endl;
    vector<string> disc2;
    for (int i = 0 ; i < 4; ++i)
    {
	ostringstream d;
	d << "disc #" << i;
	disc2.push_back(d.str());
    }
    vector<string> pilar2;
    for (int i = 0 ; i < 3 ; ++i)
    {
	ostringstream p;
	p << "pilar #" << i;
	pilar2.push_back(p.str());
    }
    hanota(disc2.begin(), disc2.end(), pilar2[0], pilar2[2], pilar2[1], ::move());
}
