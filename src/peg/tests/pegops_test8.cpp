// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include "pegops.h"
#include "pegdata.h"
#include "pegops_tests.h"

#include <string>

using namespace std;
using namespace pegops;

void TestPegOps::test8()
{
    string pegdata = "AgACAAAAAAAAAAAA4QsAAAAAAAB42m1aZ1hVVxZ9mGdsBMeCGiyDvWAU4xh1osaooLFjCZbBMqKgSewSZqJGjdEYu8ZK7GJBjKOIdRxsWGIXY4vG3mIBCzaQN++eu1a+99YX/mz2Pueee86ua5/7HI4//7s2acyZ/r4OR2DHkYtLPXO5gue8cVl/yTmGuJ7bxNUz99l81vy/4Ln59yoaegXjfqD/BU0AjY57nVTSPa/z4xOGHxuVuG+um/6M8TGxi4pY68R0Dzf81p+Cwva4+cg+jXLyu+mTTY62m93yF5jfG/vK08De2daLz82+Nre+WdFa0ycswznHzRd82MTsL6Bl/4Awt7w89v1l4SzznCPF5k9j3dYTQg3dhY0/hPwBaLfFyRez3ZTze2C9/qG5HZ56qYvxLfvCDf8x5D0gj1hs053g22P887b7W1rrRw8ec8eiH24ftektt7zSjErLMt18UczLwHNtOn164ZWb5uM+AvIYuvQtm5/bupHR1OapkectWjC/vfN6R28aA6dhndx4nue8A3oY9LXL+69MI+zjVtrqmpY+WzWtbs1ZkjKyjCWPqnbbrO/8NNLMW4TnjoLmwvuOOW06FfJBPlXNznM/fGb4nIYfDbPobxj3x3MDn2Zb6nGdgPxlkq2ZXEujM33cdAvkNTH/CPgKoFC/6+PO5dMtmuRs8NjysxKYv4Rx4Tf2pZmYHmr459X8DK3cvvVQ679UzKsZa2voPvg0+GejYyFmIBfed//KdB8TL7ttOyU06Wsc5MyBlUYlQ+Y7T1rzymGd/CUHxls2WUU/rDndnDvxyY7Z99303IHtRt7COdW88RWeS/0sI8CiRcvYjnB8wmRjj/0PNte3+JFYr9bBdYYuw3M/nHht1ik055KhvUouMeOhmN/qUX2H5Ss9C9geWB3PlQHtNarFJkuP9z+f/cg66DE8l3HJ14xfpP9g/oukwGDrRe9B3vDg3bKWvOG2iBIW/wPkjbj+9eLxxn6R9428frlYs5GA5JkpFn3nsS1PfFrDnDc+aO8uz3y0bppNy1Ursd+ij1dXsMLHFYn1r78aZcaP5WzyN3qFfNsU2yLDs2yHrbXsH2ZewLiH5kgFMI9++i54nvd30HTQDaAIU0c70IVXY80LOB/h4ehN/wW/G/wloTcGLzX+UwTz1la16dA4HzveMI95tzDm7Qc/DvzGbkWzrSd2p+UuaeyEceafzclRWyx6G/InNeyIfgN+LOOrjn3C9eBvMv8FVR1j7IX11kH+AfhfwZd71PyGtUJUm0wTeBGQF+8RaOLyhv8Zs8SMusHGxNT3zeEZg6wdTRsR+szO08OfGL+z1eBYI3aq8t3CZjke+a0y5BNH5D5r4mCjPRqL8Z1IlB3pN5AP753ezZrZAfIx1IdzraHXwO8Djek54q41vwHmM49fCbto/LkP+NG0V3Qlw3eB/JT4F+3aGuPR2YhbBBzPF48EfCe6WCuL0l8W9EXdDjlsVJAQEmVW2LN6pikR3+L5+pi/Ag5xX+rGxsG2/BbryLT+RvNdfowp5fm+1RgvUW65sVMI5H0jUZfmRpmdp6/qalS3IQnr085r7BN3xnOMJ9oJ7u84v96uPJchr00/nmx7IPEN13l/3i8LPOvj3yEnHgjLk1DEM04D+N4nK8w5Ow060sdTfm/GaONADTNODbP4tyHf0fRQXouWT24z2BpPxXqfdV1s1r/XY0I1i6ZMzGvrG+OHdpc2wOtFty6FLLoJ8hPZq02JHIX1T0I+2tnM31Pvja+dsdK0g/FYFvKCjeLNPMbCXtBHjSuYo84FHxcYa0L9cmn7ua8gf4/+C/75BjuDXUfc0T+Ik4jHGBf/fmTTNMlPtOv+3jat5vCmP2F+KfDEVcHg/UDPQD7jiNPs/3xQP/NG5psIzDsEvhh44p+ZoKwLxXfsMSe7ATn11h3jy8AHCb4i3toBvu8am0+kvTA+i3UXfNKz1Fee/ss6wvpTeXT1TM99P2P9lfjkPqmPeuIvSBsu+intwfi5muXM5Wnnp6DMS/THVsdtWgdy4pqvBVd/IH4QLPMrzj1tAHu9dBuh/YJ52ztMzPI8RybrVHvb4RqIf/yV/jFhpqHU2yvJo584/tyv6a8nUEDC4o8Z17q6J8GYNIk4ln0G+JR23nWW9dRv21LzymZcl/mltn2SOeALYbyO+OdV8dsY4ijwrMf3pK51KfHMuEB+mcf15khfQvzU0uFdP+nXXPfRQNuzJmP8MfOHnIPzfUFvxe40BmP8ElcuZL0Hz/3ESd4NFP8NFb9lHxol8bxR/J91ucasQKOSu1Lv9jD/gk6AfAj4kuK3B1jfBA8iHTraguYFZX/UDPQg6F3JA7VALxD3gfpKXaV96fc1JZ8+kXxUCXSvxOEuwaucV0T0wnzdX/bHOOc5zwtOoT/9Dfx4sRf3nyV5rDTrltj1qcxjHqMfrZU+9CtQ4vPvZH/UB/1yRbbt58TTtMckvGg75NSTU3AtcXMVyDMl/xCHkCdOTpX6s1H08KusW1Dueeh/e+Q8ediPQV5E9kV98h6j30rbgyOlDi2bV92Y+LL4OeOntuST23KeAoLfaojdUiDvBJ59fjvpi5hfGI/sn/OI/56U+A6X9zO/MQ5nsC+QuvC79Eesi6wrYZC/K/7PfnH2t1/4eOLzj8RPmU+or6MdzpT1tCvrLv19G2gS5FUlL3J+sNST2cSRUi9Zd5jv2N+dkrpP+7aQvEf8MVD6nHCp/8NAX7JPAaXfZ2C8k8v7fq4x5BUEF/K9FWVfPDfrbj/WHYnbqYIPlkt+iGEfwXso9neQA/78kScSxH/TJM+lCT6YJv7AOl5e7hv43v8JDub+moM2wfgKhzfeXAJKvdFu70ideCn5hnbjPQ37/KFiJ8bJVvAOufdYkOWdX+5IXWb+7gbahzgFPPU6TvRHHDyA+Bnys3Kf0krsWF7wAOs08/oVwd3ENSsh/1rqlZ/kX/rt96Df8P4X/GmJe/bRW8ATV82S9aNBR7yJ88JXvJ9Yl2NrJErq+UTxV+Zd+t/P4EcJniwleIJ1/qTEP/HfANDzkM8D/6HUsxjJq/Pk3nC39En/lDzCe5AUh/f98i6592E/i/bLwXth+sFl1hHBocxjR075ePUtvnLeOPHHVlKPqbd/CX6i/uOlbsWBAn78gTtYZ9dLPEwVfdcWvd2Te5LlYv8Iuf+fIvWQeeuu2JFxkyz9FO/5mov9W0s+Pij16orUgWTBJffkHsNf+oJaUu95b8d6GyH5jTh0vcQzx2mv35iXQuwOKk7uiZo5vc91W/IS9d0LPM8znXkShg0W/2OdGA/gPkXyJfMSceCP7MfS7c4+RNYZIt+zFoIWLpHjlY+YZ6bTXqwboMcFJ44AZT95TfLO6DL2+vwekST49brkWeaF/mJ3zh8pfhXp9M7rzLOnBX8N4fPghwkOTGR/gbhbJf1pXfDnXN7f9Yiju0tfW13qSpDEDfFMYfHDBbLvGLnf8hfcSL2xD2TcbBd/3iT3Bi8FBzYT3HZB5g+T7xDsB45j/BOpe4z3otJ3pAruVntw3TbyHOvsDqk73/D7s8R/Y8nDLsGzVeQ+hXl9ivQhjLu1Eve8n1oq/tFP+h7iJPZRpeS9eSVOiZeayvfkRcQx4L8AP1nye0WpF1sFn7YXu7Dejxf98b6BeYR5J5ec45x87/gPaLh8H0mX/B3r8P6uwOdGS39cTOpttnxn6ii4rqvE9Wu5j74u+PQryZ/E4axzPXa+8erPGa/P5XtMqvRl7Jd4XtpnkNxXZEjdPSl5NlH21VDun1j3Gsh90EK51zgh93X0g/kSD7TzAMHJ+QSv3ZH7ZdpxhKwbLn4YJOsflvzCPmw18a7k9SPyu4UjkmfXSz8eLHncX+6baJ9Q+T3BYYm72xJH16UO9BF82U5wWj3ps0sJbn1b7k3ZjwVKnOUS/obkz8uCg09Jfuc9+VbJG9vkfrO55BPqZajk+VoSx+Wlz3gs++b35xDx81TJQ12kD+C896WO7ha81Vj6mgfy+4JJ7DfO2xmS98vfR9hAy/dLe6c9+b23ivd9EOsV+7VwNAr5fJuYkPw/RZRX/i0zAAAAAAAALDMAAAAAAAADAAMAAwAAAIljRdYBAAAA9omA1GkMAABq7cWqawwAAAGeOYs2MgAA";

    CPegData pdUser1(pegdata);
    if (!pdUser1.IsValid()) {
        QVERIFY(pdUser1.IsValid() == false);
    }
    
//    QVERIFY(pdUser1.fractions.Total() == (pdUser1.fractions.High(pdUser1.peglevel) + pdUser1.fractions.Low(pdUser1.peglevel)));
    
//    CFractions l = pdUser1.fractions.LowPart(pdUser1.peglevel, nullptr);
//    CFractions h = pdUser1.fractions.HighPart(pdUser1.peglevel, nullptr);
    
//    QVERIFY(l.Total() == pdUser1.fractions.Low(pdUser1.peglevel));
//    QVERIFY(h.Total() == pdUser1.fractions.High(pdUser1.peglevel));
    
//    pdUser1.nReserve = 0;
//    pdUser1.nLiquid = pdUser1.fractions.Total();
//    qDebug() << QString::fromStdString(pdUser1.ToString());
    
//    {
//        string pegdata1 = "AgACAAAAAAAAAAAApAkAAAAAAAB42oWZe3BV1RXG7yvPm1ySmwRjtC0tdNrBAazQAbFAC4papEI76OhgrTOgpYUyTCVKp9oHlZYKThBQkUcEdCyjQhSkQ0dhkIJUyGBxxApC5BUIed28bu5N7qOcffZizv5N9nj/Wdn77LP2enzrW3ufxJ++UBbx+XxdWfeX0nJIeMZbjrxl4mg11XDInT+hn/v0b3jVBL8j/XpcfzCkpOg5p2WvlnEtx3/vtxuDV9dN7U6qcfOeo3M6r8qkft7wzScDjp78719R452zb7298ur42E3vPOLs9a1Q3Ueiy/nVa3nzoYyS72bGKZOWRduGODonHqg+Gb06/nh+vrIvsmPsGEdzjrb7379w3y+qdMd9Wt8945qUrO5xx2k9n9GyOuf6jY7s1+NirS9Qp6UeN+rnlZcK1Thfz1/R87t1AhJ6HNbPd51d/Tdn/GTR9A2OXP1Y9Alnfve/Au84NgT1OrHnyPnGWx0p+37nDVcu1uPyBR+qdQ/+6kP1ysGxrqebhr+X9foh74u/IrvS4qH7k7z/N8+V1W8f2ub8+Z/1UwY56/1/PqFMvGOaq3/N5iK1riNr+it61mnZrudf+2yuGg9Z73r47OZvqJBJfiR/z7VvUmPBz4j1rgf71t6tAq7Tl5W4y7ovxS8t13V8qVwduvGj/d48DdbyLxsiat2oFvfJXYddy4OvHN/h/FWl152brutIjz/X+ktW3Gnse19winq+O+muO7Ilo+ztn7dIjR/dFVem5mo9E3orRjgGij8vvFTj6vndlN848yer3Kq41O3uuPAW7e/SlU6Z+56Y445nnqtTz5t2TlGZi2l99891Nesy8DXf1KDGK2acUrLmlBupJr3+VMO8WmddiUagxEvy0vL+nvuduEzaNm6it66Wb/IbPCH+nT3x8kFnfEHPv3HkdkUoe7/7maKSTuSxaPZz3c74cI2L0PxYoTLk8ZfOK8pZu8Sd3zv+NSVPT9jW6+Wj9zQQ316wWEEx9ctlRh2/v9lFYm3FcrV/gZ5f8IDLc8+/ovliRbOLo6mz+r3+fFU9iezBukItG0bVKCk8J/Xeosch4DkFvXnTCvq8eifqxNSPNt/LYH+py6geD0n1TFJ5/uLRoNceyfMLD8QHe/2bPdWgiWv1POcxk19FjjleG/Puz3qVdaUnf/yUM65df1oBSHA4c3JAyTM79vvdOqru9cbjke0rf+LM/+nby5SrffOnKTlM6++EP3e8O/Kwp71cs6Phvn3KlPOfuvOShxfTJv9LHCZM/ukRRxYiDnsLegzeEzxO/fXRL7zr5fkHe1vUkgj0VB9zERAG3iSfkl/Rt1KPj2sgiX+z9PPsw48riAvOarSDp0YFVGgOXLdCjSfftlM9b9PvS11o2r2mV+y5OWLOn1s6Qo0/vmut2kpwLD29pG6VcqFIz8/Tdhz+3ydq/bofDlOpnRs3zxfBu5NG/5T4tYFnfr44aeBP8vv3XZrPhV+EB5cO7fLiKdeEt6/in0sC3rG8//LpMUruT81q8dbrwiXHlJ6HnlkT8Z4TVufeoAwYdmNTq9cv/yT3PHRx0YMqD41Rv1EniaFvKtXVV0YGvPNPNe1R4zLU0f6fvRXw8vT00of8A/FxaNUewy/B0++fn5T29s2yRduVPKQdl/6eh32jL2oeQp0EEDeZP5o2+UzskPX36ARJ/sTubviRBH+GUC/h1lolt205nB7oPJcAP0icOrDu8w3jDL054HPalYJdku/RFSYPCg93oN62l48z4sw+0lm1NZMd4JzI/uNHnvIs/Yt1LfbvLf+RsT6LuIjf+5Km/QWoT/JtCvaIH/UPL1Hzq4YGDbsXNi0csA/OH27mIY7+efnTqIGrLPCWjz6Ri3NOrg58qNTtS09v3m3kU3i7Vfi40Iy3/Cb/YGbaGx/Bre/VjHFOzQHe2f8HcT/wFePqv/PMgPWVxPmEdtlwLfIZfTGLIR8p+JGDff94fKxRp2In/Q+jLkKo7z7cp+hPBHHpRVyk7qsXbDXsz8d60VuGuDNPCdRTP+qffUv0XsF5KY34k2dTiEcn9iF+g8CF34KvPPjNeg1a+LYU/pL3mac49ouBT4KQ/cBfEHVM3gohzt3wuwx8GbPgSPS83mDiQOz+BPEKwT7mMxd2y/5FsIf1WWDhNfbzAPDej3s41/He4cO87NvsHnN8EdixdX6e3+unH3xKPKUQ5xDqKQ/2xYGLOOLVY+kfcdiTY4lnBPetfNTxYIt9/D7C80vYwlOSj9eDI4338+FPLvSG6ieHvHHhPUTeu0HPnwd/FCEfSZxvEvBPZBT1mUT8cxG/HOCY/NuMOJcAb3F8T/w69JOP2D/S8KMJY8m79M9zsLscee3A96NBwJfkowpx7UNewsBBCerCjziH4Q/PpT7LubUEfl4GjuPwh3wasPATz7mdyFcE+c3BfrxvLQI+M5b6CqP+ysHn7YhfCPURB/+G4EcC9iUR/wRwT37vQT55viIvdmB9O/DCPFcBBzGsEz/Lt5YZ/sv64osBow5C6NfsHzHwSLulfwXwfhJ2yfpW1HE36lfyw/NDD+ztQx9otZzTbkScmV/RWwF74rDzLPaXun91vOk3492NuuF5j98rQpb7Rwh8dMYSf9Z1DHG4BLxloacLddcCfilDHRRDD/tlAnUdx7gzO/D9sB/zaejjebSP3zHQ51lHRRZcMh69uI+n0Se/Jjz2D/M+IHqWg7d4Hu0CXwpOJC5D9fwF4FbsqLPEkecceX7vRjP+UqdtsENwtuYDs54DeC7/5zpv+f5Ygv1535hzwOTtnq/4XjIIuMxgfSniupLfGaG3H/Mx9PUY+uVpPb7OEvc+8Af7Tx7OGYWo94TlXt0NPknB7qClDwWBh7ilj/Raztt9lvteP+ZjqEPu0wQeKQA+M6hv4jYCv/i9nN+/A3g/bLHbdj8Koq+EwH9x8EClxY5uxLUN/bMYuA4inn7LfbkAca3E+S6CvpcD/PkRzzRwHMD7Uct3J+I3a/kOnAAuiiznCd4DiM82vE98M49h8HiR5ftjruU8XWq5V0qcLu428cb7fBL2JyzfNXhv4veMKOKZRDzzfQN/Fxd5ETiusPRpnrO7LPhpRtz8sDcNfk2DVzuA017sI3G4HjjJYL4VPMj48H7P8wjPPUFLXhotPF9uqfsU8l4MPiq2/N8zD+eXDuQlF/vxfO23fHdj34sjPzx/8/yesPy/mt872yx9OW0ZZ+BfFDyWBD7LgE/mmd9x+2B3AfJN3pF4/+E2Mw+X39T31HtN3FXmmHwdg1+NOtFb/jpDqfo/YMJ5fX40AAAAAAAAfTQAAAAAAAADAAMAAwAAAEc1jIwlAQAAUwlsc6YMAABYpdg+OQAAALIxm/ACBQAA";
    
//        CPegData pdUser1(pegdata1);
//        if (!pdUser1.IsValid()) {
//            QVERIFY(pdUser1.IsValid() == true);
//        }
        
//        //qDebug() << 
        
//        int nSupplyEffective = pdUser1.peglevel.nSupply + pdUser1.peglevel.nShift;
//        nSupplyEffective++;
        
//        int64_t nLiquidWithoutPartial1 = pdUser1.fractions.High(nSupplyEffective);
//        int64_t nReserveWithoutPartial1 = pdUser1.fractions.Low(nSupplyEffective-1);
        
//        qDebug() << "nLiquidWithoutPartial1" << nLiquidWithoutPartial1;
//        qDebug() << "nReserveWithoutPartial1" << nReserveWithoutPartial1;

//        string pegdata2 = "AgACAAAAAAAAAAAACAQAAAAAAAB42s1YTUhVQRSeuU+fL5+WC4n+Hki16Edr40ZU0oJSkH6oRZaRLYKMoF0Lg/5WGiVmitQiwVpYEFQUEbooRSulhCIsUzNFCcsiMh7qe+9md84NZph5d+69cx/N5tyZM3PmnO/8zMw93zJSpyGELldEBvWF1nkviP623NBcW3Shv2JGNxpiGgY64SNUg/4Shi9al1ZJ9vlOxOu3N78zqMnvTiG0A/gNB8uM/sz+JINW+ceNcY2RH4H5H0Cvocopo798b77RXwnzzhxvp/aDZTqr5xMYxwI7+l7EuPhoAvvXCOSIWl4NkV/aQ+jdU2RFxsQ8V6879VFKHx/D3wX2rAOgTPyzBfaz/WwLfmTVMoOeTSV6ZKaR8Y0gf71FXLD9Ykz7dUM73x/9MXp8EdDDWXL7mG3fLJHTe0Tj8sEsPQR9P9DencnUPDaezNYRpfV8y/jp+mpCj8IEbZqWszUUpta3gt3lU2R8DPatZeK2L4eOywDwh5bSOHxi1l2Dfh3QQYZ/4zf5mhPET2Meyde+LhoHvwD/4hJazsl0Qotu8eXHPpJxcJs+AB/Vk/T4OODUNcGPn+1fyPgFJo5ew/rSm/T+i4EmAX0K8/b8lMsjLMnvxnL1VCSnYDp+/caSebjtJaGPUvl6FJfQ80X1T2PkmnnS0i+HW9FF8jWaTI9/hgJRA+NrGT9vChNaQcqm3joXH5dDu/n86h861az8V3iAP29EUB8KrtDzTdo8yccnw8KfyGb8PQvx13XBwd4E+ZGr8ddrkvFUD3RHkM8/N8/HLQjnyfAvPv+rzsePlf9NgL9fgFvtAB//f/U1hb+uDWhWJ63XMdh/LIeefxoOgszn9PwekP8+g647Jv8hFKRRxoCmAB+HEwG6X5bEx+lqgG9XeZjBT4+Pjyj/Ze8BsnXUqdw3ENcps3J1SCTngU397OrLtkvR+HXs8Xx8PnKpl9le2fQfcjleDv4KMuP5Mbk4ZPn5DvVn5w3D+VOkOau/9WG+vyKC88apP9l+sy53T3Cah3bj4L5i+9z2R2J8fJDD/EYO5yGX9URV/Ni93+qS5zJWbK8qHFTt67bvVD8rvUz/aA7rn2hdoc/ZOWj1jsCKcPUqLtzmT4NNPavSvdXHbVy7xT3R+COHeeDVfU92X7t2YI/lWNV92Xej6jzECaonTuNZdZxsEYynehT3Xtdf1edoouy1+l/iVT3GLuuI7HqkOL6dvvdV1w27+onqntv3gKr/IUigp6pzQNW7R9X/IFW4e+Uft+dKouslVmSv1+9jVXh49T7DLuMDO8RHVfyo+p+X6PeFqrhGCcoPK3mazXuF2/zySh72mK/6Hu/V+eHVe1HVvfV/+Z+V6PptUp/L93Qj/K/5A3qV/+h+NAAAAAAAAH00AAAAAAAAAwADAAMAAABHNYyMJQEAAFMJbHOmDAAAcudfXwcAAAAAAAAAAAAAAA==";
    
//        CPegData pdUser2(pegdata2);
//        if (!pdUser2.IsValid()) {
//            QVERIFY(pdUser2.IsValid() == true);
//        }

//        int64_t nLiquidWithoutPartial2 = pdUser2.fractions.High(nSupplyEffective);
//        int64_t nReserveWithoutPartial2 = pdUser2.fractions.Low(nSupplyEffective-1);
        
//        qDebug() << "nLiquidWithoutPartial2" << nLiquidWithoutPartial2;
//        qDebug() << "nReserveWithoutPartial2" << nReserveWithoutPartial2;
        
//        pdUser1.nLiquid += 28641475482;
//        pdUser1.nReserve = pdUser1.fractions.Total() - pdUser1.nLiquid;
        
//        qDebug() << "repack1" << pdUser1.ToString().c_str();

//        pdUser2.nLiquid = 267497982;
//        pdUser2.nReserve = pdUser2.fractions.Total() - pdUser2.nLiquid;
        
//        qDebug() << "repack2" << pdUser2.ToString().c_str();
//    }
    
        {
//            string pegdata1 = "AgACAAAAAAAAAAAAjgMAAAAAAAB42ps9W7jrEgMDg8+L7uIX////t/26hBnIZVA6wv/MC8j/a3juCZD6L8fIAAZQCg6KE1DF43GoQ+dntV0C02///geDR8nz/oBoBiLB/OA0Jmzm80HpnHwIvTVyO9iGYIcFbCA+P1QXW33jT1LsGwWjAB849lMXJYkfgyTr/+jpc/dhVPFJIhA6CMrPnQHOfgznlSD8d2z/wOph+i/Uf/1HTrr1s40B01JQ/pY3EHeoHmJEcV8mH6p7eX5A1PF9h9Aif1Hd//zh6v+j+WgUUAq8zkegpDtc9ceBfjkwzVWCKi99CJIu5fMg/G+v/qKky3RNYTB9fTFEXctU7PkTBlqSPmFN17t2QPLjN2j+ZiTgrwrdz1jtCfuLqh9G52R+xyp+hP0fVvu0cdS3uPwFA0w45PP8mFDE/0D9ecgCIrL7MYS/QZu4eh7GZ0ETl+KG0PvvY9e3fBlq+MDcwfQewhfih/D/osVD7L//WOMNPTzT/mEP/2wc4fXkzh+s6mFg6izs+p7xQOgM098o+mW+QOwv6mNGMY/LFRoP01DVW0LN9/CDiDijpw9lCMu3A1XcdhV29+osR+WrhWJ3v/oq7OGnG4IaftJv/uMNHxhflUA6ZSSQbtEBMwNx6ZCBgL0TX0DcP70Uf7lAyH0XeElzBy55NiL9FXseu3th9LNV+OUZiYwPQuIP+Ykz7x+OcpORxHShdR9izhE09bfvYDefm4D7l7AQV47hchfMX/HQeqlHB7/7mXGYFyCLXf3q/8TlL1LDFQaOf8cfL8Sag87/+h9/eYxLfB4vae4gNn2TGz45L/GXC6SGC6X5kFrxw0BmODGSGf6UhhMDheFKajuJWPGf//GXs5SKkxoP1IoXYuOZF4d6RwfKwpfSeCc3/KmVf0n1FwxYcZPm/88ujFQJV0rzJaX5iFrlN6XlGrnmMtLIXkYc/Tdqxw+16k1GGsU/veON2PKFWuU5A5XKIXT6NAt2e7bSuD1BaTqndjgx0Dh/8lPY72MYJPmP2vFL7fzPQGH6oVW6gYE3BMYHaZUeSY2vF/+JG++AgU9E9t/JjYdPOPqL5IYDvdrZjFSOF2r3wxmp7I6B5g8Ve6nlbnqro7TeoFZ9z0hj86gt/57C8U1a1cvU7l8z0Kh8YqBx+qVV/4xa7RlyxzVoPa5G6fgJpe4j1Z2U2oNO86HxtaC0CJo4EwF7WKG0Di9EBADB9N/PqjQAAAAAAACpNAAAAAAAAAMAAwADAAAAzW+FbjQBAACWTqRCpgwAAHM6lgxDAgAA0/OWAwAAAAA=";
        
//            CPegData pdUser1(pegdata1);
//            if (!pdUser1.IsValid()) {
//                QVERIFY(pdUser1.IsValid() == true);
//            }
            
//            //qDebug() << 
            
//            int nSupplyEffective = pdUser1.peglevel.nSupply + pdUser1.peglevel.nShift;
//            nSupplyEffective++;
            
//            int64_t nLiquidWithoutPartial1 = pdUser1.fractions.High(nSupplyEffective);
//            int64_t nReserveWithoutPartial1 = pdUser1.fractions.Low(nSupplyEffective-1);
            
//            qDebug() << "nLiquidWithoutPartial1" << nLiquidWithoutPartial1;
//            qDebug() << "nReserveWithoutPartial1" << nReserveWithoutPartial1;
    
//            pdUser1.nLiquid = 2475904104;
//            pdUser1.nReserve = pdUser1.fractions.Total() - pdUser1.nLiquid;
            
//            qDebug() << "repack1" << pdUser1.ToString().c_str();
            
//            string pegdata2 = "AgACAAAAAAAAAAAACAQAAAAAAAB42s1YTUhVQRSeuU+fL5+WC4n+Hki16Edr40ZU0oJSkH6oRZaRLYKMoF0Lg/5WGiVmitQiwVpYEFQUEbooRSulhCIsUzNFCcsiMh7qe+9md84NZph5d+69cx/N5tyZM3PmnO/8zMw93zJSpyGELldEBvWF1nkviP623NBcW3Shv2JGNxpiGgY64SNUg/4Shi9al1ZJ9vlOxOu3N78zqMnvTiG0A/gNB8uM/sz+JINW+ceNcY2RH4H5H0Cvocopo798b77RXwnzzhxvp/aDZTqr5xMYxwI7+l7EuPhoAvvXCOSIWl4NkV/aQ+jdU2RFxsQ8V6879VFKHx/D3wX2rAOgTPyzBfaz/WwLfmTVMoOeTSV6ZKaR8Y0gf71FXLD9Ykz7dUM73x/9MXp8EdDDWXL7mG3fLJHTe0Tj8sEsPQR9P9DencnUPDaezNYRpfV8y/jp+mpCj8IEbZqWszUUpta3gt3lU2R8DPatZeK2L4eOywDwh5bSOHxi1l2Dfh3QQYZ/4zf5mhPET2Meyde+LhoHvwD/4hJazsl0Qotu8eXHPpJxcJs+AB/Vk/T4OODUNcGPn+1fyPgFJo5ew/rSm/T+i4EmAX0K8/b8lMsjLMnvxnL1VCSnYDp+/caSebjtJaGPUvl6FJfQ80X1T2PkmnnS0i+HW9FF8jWaTI9/hgJRA+NrGT9vChNaQcqm3joXH5dDu/n86h861az8V3iAP29EUB8KrtDzTdo8yccnw8KfyGb8PQvx13XBwd4E+ZGr8ddrkvFUD3RHkM8/N8/HLQjnyfAvPv+rzsePlf9NgL9fgFvtAB//f/U1hb+uDWhWJ63XMdh/LIeefxoOgszn9PwekP8+g647Jv8hFKRRxoCmAB+HEwG6X5bEx+lqgG9XeZjBT4+Pjyj/Ze8BsnXUqdw3ENcps3J1SCTngU397OrLtkvR+HXs8Xx8PnKpl9le2fQfcjleDv4KMuP5Mbk4ZPn5DvVn5w3D+VOkOau/9WG+vyKC88apP9l+sy53T3Cah3bj4L5i+9z2R2J8fJDD/EYO5yGX9URV/Ni93+qS5zJWbK8qHFTt67bvVD8rvUz/aA7rn2hdoc/ZOWj1jsCKcPUqLtzmT4NNPavSvdXHbVy7xT3R+COHeeDVfU92X7t2YI/lWNV92Xej6jzECaonTuNZdZxsEYynehT3Xtdf1edoouy1+l/iVT3GLuuI7HqkOL6dvvdV1w27+onqntv3gKr/IUigp6pzQNW7R9X/IFW4e+Uft+dKouslVmSv1+9jVXh49T7DLuMDO8RHVfyo+p+X6PeFqrhGCcoPK3mazXuF2/zySh72mK/6Hu/V+eHVe1HVvfV/+Z+V6PptUp/L93Qj/K/5A3qV/+h+NAAAAAAAAH00AAAAAAAAAwADAAMAAABHNYyMJQEAAFMJbHOmDAAAcudfXwcAAAAAAAAAAAAAAA==";
        
//            CPegData pdUser2(pegdata2);
//            if (!pdUser2.IsValid()) {
//                QVERIFY(pdUser2.IsValid() == true);
//            }
    
//            int64_t nLiquidWithoutPartial2 = pdUser2.fractions.High(nSupplyEffective);
//            int64_t nReserveWithoutPartial2 = pdUser2.fractions.Low(nSupplyEffective-1);
            
//            qDebug() << "nLiquidWithoutPartial2" << nLiquidWithoutPartial2;
//            qDebug() << "nReserveWithoutPartial2" << nReserveWithoutPartial2;
            
//            pdUser1.nLiquid += 28641475482;
//            pdUser1.nReserve = pdUser1.fractions.Total() - pdUser1.nLiquid;
            
//            qDebug() << "repack1" << pdUser1.ToString().c_str();
    
//            pdUser2.nLiquid = 267497982;
//            pdUser2.nReserve = pdUser2.fractions.Total() - pdUser2.nLiquid;
            
//            qDebug() << "repack2" << pdUser2.ToString().c_str();
        }
}
