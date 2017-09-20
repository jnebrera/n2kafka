# Meraki decoder

Meraki decoder is able to communicate with your meraki scanning and location
infrastructure. In order to do that, you have to configure your n2kafka endpoint
under "Network wide > configure > general > Location and scanning" in your
meraki dashboard.

In order to not to have to store all meraki validators, you have to configure
your POST URL as `http://<n2kafka>/v1/meraki/<validator>`. This way, n2kafka
will know the right response for meraki cloud.

Remember to configure a default (or per-listener) topic for meraki decoder. If
you don't do that, meraki decoder will not be able to forward any message to
kafka cluster.
