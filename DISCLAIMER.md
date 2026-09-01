# Legal & Safety Disclaimer

**READ THIS CAREFULLY BEFORE USING OR DISTRIBUTING THIS SOFTWARE.**

---

## 1. Non-Warranty & "AS IS" Provision

This software, compiler toolchain, runtime libraries, intermediate representations, verification engines, examples, and associated documentation (collectively, the **"Software"**) are provided by the author(s), maintainer(s), and contributor(s) **"AS IS"** and with all faults.

**TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW:**
- THE AUTHOR(S), MAINTAINER(S), AND COPYRIGHT HOLDER(S) EXPRESSLY DISCLAIM ALL WARRANTIES, WHETHER STATUTORY, EXPRESS, IMPLIED, OR OTHERWISE, INCLUDING WITHOUT LIMITATION THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, AND NON-INFRINGEMENT.
- IN NO EVENT SHALL THE AUTHOR(S), MAINTAINER(S), OR COPYRIGHT HOLDER(S) BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, PUNITIVE, CONSEQUENTIAL, OR SIMILAR DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, REVENUE, PROFIT, BUSINESS INTERRUPTION; OR PERSONAL INJURY, BODILY HARM, OR LOSS OF LIFE), ARISING IN ANY WAY OUT OF OR RELATED TO THE DESIGN, COMPILATION, EXECUTION, MODIFICATION, OR USE OF THIS SOFTWARE, REGARDLESS OF THE CAUSE OF ACTION AND THEORY OF LIABILITY (WHETHER IN CONTRACT, STRICT LIABILITY, TORT INCLUDING NEGLIGENCE, OR OTHERWISE), EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

---

## 2. Safety-Critical & Tool Qualification Disclaimer

The Software is an experimental, open-source compiler and runtime framework intended solely for research, educational, modeling, and software engineering development purposes.

1. **Not Pre-Certified**: The Software has **NOT** been certified, qualified, approved, or audited by any regulatory authority or certification body, including but not limited to:
   - Federal Aviation Administration (FAA) / European Union Aviation Safety Agency (EASA) under RTCA DO-178C / DO-330 / ED-12C.
   - International Organization for Standardization (ISO) under ISO 26262 (ASIL A-D).
   - International Electrotechnical Commission (IEC) under IEC 61508, IEC 62304 (Medical), or IEC 60880 (Nuclear).
   - Food and Drug Administration (FDA) or European Medicines Agency (EMA).
2. **End-User Qualification Obligation**: The end-user, system integrator, or deploying organization assumes **100% sole and exclusive legal, technical, and regulatory responsibility** for:
   - Tool qualification (e.g. DO-330 / ISO 26262 tool confidence level assessment).
   - Verification, validation, testing, fault injection, and static/dynamic code analysis of all emitted code and runtime artifacts.
   - Ensuring fail-safe hardware interlocks, manual overrides, and human safety protections prior to deployment in any life-critical, mission-critical, or high-consequence environment.

---

## 3. High-Risk Activities Disclaimer

The Software is **NOT** fault-tolerant and is not designed, manufactured, or intended for use or resale in hazardous environments requiring fail-safe performance, such as:
- In-flight aircraft navigation, flight control, collision avoidance, or air traffic control systems;
- Life support systems, implantable medical devices, or surgical robotics;
- Direct nuclear facility controls, weapons systems, or critical infrastructure whose failure could directly lead to death, personal injury, severe physical damage, or catastrophic environmental disaster (**"High-Risk Activities"**).

Any person or entity deploying the Software in High-Risk Activities does so **entirely at their own risk and peril** and agrees to indemnify, defend, and hold harmless the author(s) and maintainer(s) from any claims, suits, liabilities, or losses arising from such unauthorized use.

---

## 4. Third-Party Trademarks & Proprietary Formats

All product names, logos, trademarks, and registered trademarks referenced in this repository are the property of their respective owners:
- **OMG SysML®** and **UML®** are registered trademarks of the Object Management Group (OMG).
- **Cameo®** and **MagicDraw®** are registered trademarks of Dassault Systèmes / No Magic, Inc.
- **W3C SCXML** is a standard of the World Wide Web Consortium (W3C).
- **nuXmv** is a formal model checker developed by Fondazione Bruno Kessler (FBK).
- **PlantUML** and **Mermaid** are independent open-source projects.

Reference to these third-party trademarks and standards is strictly for **descriptive, interoperability, and educational purposes (nominative fair use)**. The author(s) and maintainer(s) of `fsmc` claim no ownership of these marks, and use of these names does not imply any affiliation, sponsorship, endorsement, or certification by their respective owners.
