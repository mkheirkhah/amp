
<!-- **Table of content** -->
<!-- - [AMP and DCM](#amp-and-dcm) -->
<!-- - [Installation](#installations) -->
<!--   - [Mac](#mac) -->
<!--   - [Linux](#linux) -->
<!-- - [Simulations](#simulations) -->
<!-- - [Contact](#contact) -->
<!-- - [How to cite this code-base?](#how-to-cite-this-code-base) -->

# AMP: An Adaptive Multipath TCP for Data Center Networks

In this research, we proposed two novel ECN-capable multipath
congestion control algorithms for modern data center networks (DCNs).

The first algorithm is called Adaptive MultiPath (AMP) that is
particularly designed to be robust against the TCP incast problem. It
also coexists well with single-path flows like DCTCP, preventing the
Last Hop Unfairness (LHU) problem that we've reported in this paper
for the first time. In addition, the design of AMP is simple with low
overhead. AMP moves traffic quickly from congested paths to less
congested ones without sophisticated mechanisms such as RTT-dependent
congestion window increase, as in standard MPTCP, or dynamic
congestion window decrease, as in DCTCP.

The second algorithm is called Data Center MultiPath (DCM), which
integrates DCTCP with the standard MPTCP congestion control algorithm
-- Linked Increases Algorithm (LIA). To the best of our knowledge, we
are the first to do such an integration.

<!-- DCM has been initially -->
<!-- presented by the name of Data Center MultiPath TCP -->
<!-- ([DCMPTCP](http://coseners.net/wp-content/uploads/2015/07/Kheirkhah_MSN16.pdf -->
<!-- "Low Latency MultiPath TCP")) at the Multi-Service Network workshop -->
<!-- ([MSN16](http://coseners.net/previous/msn2016/)), in UK, on 7th July -->
<!-- 2016. -->

The AMP paper has presented at the IFIP Networking 2019 Conference in Warsaw, Poland. [Paper](https://ieeexplore.ieee.org/document/8816848) -
[Extended Version](https://arxiv.org/pdf/1707.00322.pdf).



# Implementations
As part of this project, we have implemented several networking
protocols within ns-3.19. A few of our implementations are listed bellow:

* **AMP**    : Adaptive MultiPath TCP
* **DCM**    : Data Center MultiPath TCP
* **XMP**    : eXplicit MultiPath
* **MPTCP**  : MultiPath TCP
* **MMPTCP** : Maximum MultiPath TCP
* **DCTCP**  : Data Center TCP
* **ECN**    : Explicit Congestion Notification
* **ECMP**   : Equal-Cost Multi-Path

# Installations
## Mac
1. Install gcc-5 with wonderful Homebrew. If you are using MacPort, then you should
   install gcc4.3 (or at least I tested it with this gcc version)

``` shell
brew install gcc@5
```

2. Clone the AMP's repository 

``` shell
git clone https://github.com/mkheirkhah/amp.git
```

3. Configure and build ns-3 with g++-5 

``` shell
GCC="g++-5" CC="gcc-5" CXXFLAGS="-Wall" ./waf --build-profile=optimized configure build

```
4. Run a simulation

``` shell
./waf --run "incast"
```
    
## Linux

I have tested this source code atop Ubuntu16.4 with gcc/g++5.4.0.

1. Clone the AMP's repository 

``` shell
git clone https://github.com/mkheirkhah/amp.git
```

2. Configure and build ns-3 with 

``` shell
CXXFLAGS="-Wall" ./waf --build-profile=optimized configure build 
```

3. Run a simulation

``` shell
./waf --run "incast"
```

# Simulations

All simulation configurations are available [here](./scratch/).


# Contact

``` shell
Morteza Kheirkhah, University College London (UCL), m.kheirkhah@ucl.ac.uk
```

# How to reference this source code?

Please use the following bibtex:

```
@inproceedings{amp2019, 
author={Kheirkhah, Morteza and Lee, Myungjin},
booktitle={IFIP Networking Conference},
title={{AMP: An Adaptive Multipath TCP for Data Center Networks}},
year={2019},
pages={1-9},
doi={10.23919/IFIPNetworking.2019.8816848},
ISSN={1861-2288},
month={May},}
```
