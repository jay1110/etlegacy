# Legal and privacy gap analysis

Last reviewed: 2026-09-24

This is an engineering checklist, not legal advice. It records what the code
and production configuration establish, which public notice fields are still
missing, and which decisions need the operator or qualified German counsel.
It does not disable maps, mods or any other feature.

## Primary legal references

- [Article 13 GDPR](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32016R0679)
  requires, among other items, the identity and contact details of the
  controller, purposes and legal bases, recipients, retention information and
  data-subject rights when personal data are collected.
- [GDPR Article 6 and recitals 47 and 49](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32016R0679)
  provide the framework for legitimate-interest processing. Recital 49
  specifically recognises strictly necessary and proportionate network and
  information-security processing as a legitimate interest.
- [Section 5 DDG](https://www.gesetze-im-internet.de/ddg/__5.html) lists the
  provider information required when its scope applies, including name,
  postal address and a means of rapid electronic contact.

Whether a particular non-commercial hobby service falls within every German
provider-information rule depends on the concrete operation. The fact that
the upstream ET: Legacy website has no visible imprint is not evidence that
this deployment is exempt. A final assessment must be made for this service.

## Facts established from the deployment

- The operator is a private individual based in Germany.
- The service is non-commercial: no advertising, paid access or mandatory
  donations were reported.
- The public infrastructure is hosted at Kimsufi/OVHcloud on
  `135.125.189.21`.
- Plesk/Nginx keeps HTTP, HTTPS and WebSocket access/error logs. Rotation is
  based on a 10,240 KB size threshold with ten compressed generations, not a
  maximum number of days.
- The PM2 relay and lobby run as `root`. No PM2 log rotation was detected.
- The lobby advertises Google's public STUN service
  `stun:stun.l.google.com:19302`; no TURN service is configured.
- Browser-hosted games can expose peer network addresses to the other peers,
  and the fallback relay can carry game packets through the operator's server.
- Map/mod downloads and joins to public game servers can disclose connection
  metadata and game identifiers to independent third-party hosts.

## Blocking information for a publishable privacy notice

A complete Article 13 notice cannot be generated from the repository yet.
The following operator-controlled information is missing:

1. Controller identity and a contact channel for privacy requests.
2. A defined maximum retention time for Plesk/Nginx logs.
3. A defined maximum retention time for PM2 application logs.
4. Confirmation of the OVHcloud/Kimsufi data-processing terms applicable to
   this account and service.
5. The chosen legal basis and documented balancing test for each processing
   purpose, especially abuse prevention and longer-lived log data.
6. The competent data-protection supervisory authority, which depends on the
   operator's German federal state.
7. The public abuse/contact process for server-list entries and relay misuse.

The controller's name and address should not be guessed or committed without
the operator's explicit publication decision. Omitting them from a final
privacy notice, however, would leave the Article 13 controller-identification
requirement unresolved.

## Remediation order

1. Set a calendar-based log-retention maximum in Plesk and PM2; document the
   chosen days and verify deletion with an operational check.
2. Run the relay and lobby as a dedicated unprivileged service account.
3. Decide whether to keep Google STUN or operate an own EU-hosted STUN/TURN
   endpoint. Record the recipient and transfer assessment either way.
4. Complete the controller/contact and supervisory-authority fields privately,
   then generate a public privacy notice from the verified facts.
5. Obtain a German legal review of whether Section 5 DDG and any state media
   law require a public provider notice for this concrete hobby service.
6. Link the final privacy and provider notices from every public deployment,
   not only from the GitHub repository.

