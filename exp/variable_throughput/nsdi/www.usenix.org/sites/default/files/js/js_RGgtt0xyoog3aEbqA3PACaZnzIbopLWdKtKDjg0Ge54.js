var p="http",d="static";if(document.location.protocol=="https:"){p+="s";d="engine";}var z=document.createElement("script");z.type="text/javascript";z.async=true;z.src=p+"://"+d+".adzerk.net/ados.js";var s=document.getElementsByTagName("script")[0];s.parentNode.insertBefore(z,s);
var ados_keywords = ados_keywords || [];
          ados_keywords.push('T-SSL');
	  ados_keywords.push('S-Jobs');
	  ados_keywords.push('S-Students');
	  ados_keywords.push('S-Conference');
	  ados_keywords.push('S-Membership');
	  ados_keywords.push('S-Lisa');
	  ados_keywords.push('S-Publications');

var ados = ados || {};
ados.run = ados.run || [];
ados.run.push(function() {


    /* load placement for account: linuxnewmedia, site: Usenix, size: 728x90
     * - Leaderboard, zone: Leaderboard*/
    ados_add_placement(4669, 33580, "azk70484_leaderboard", 4).setZone(34854);

    ados_setKeywords(ados_keywords.join(', ')); 
    ados_load();
    });
;
