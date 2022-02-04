/**
 * @file
 * Custom code for bibtex entries
 */
(function ($, Drupal, window, document) {
  'use strict';
  Drupal.behaviors.bibtexAccordion = {
    "attach": function(context, settings) {
       $('.bibtex-accordion', context).once('bibtex-accordion').each(function(i, val){
        var $accordionContent = $(this).find('.bibtex-accordion-content');
        if ($accordionContent.length) {
          $accordionContent.hide();
          $(this).find('.bibtex-accordion-label').bind('click', function(e){
            $accordionContent.slideToggle(700);
          });
        }
      });
    }
  };


})(jQuery, Drupal, this, this.document);
;
/**
 * @file
 *
 */

(function ($, Drupal, window, document) {
  "use strict";
Drupal.behaviors.mobile_nav = {
  attach: function(context, settings) {
    let fullMenu = '.usenix-og-auto-header-menu ul.menu';
    if ($('.tb-megamenu').length) {
      fullMenu = 'ul.tb-megamenu-nav';
    }
    $(fullMenu).slicknav({
      prependTo: '#site-header',
      label: '',
      allowParentLinks: true,
      closeOnClick: true
    });
    $("ul.slicknav_nav .tb-megamenu-block").remove();
    $("#usenix-login-bar-links .login").clone().removeClass().appendTo("ul.slicknav_nav");
  }
};

})(jQuery, Drupal, this, this.document);
;
/*!
    SlickNav Responsive Mobile Menu v1.0.2
    (c) 2015 Josh Cope
    licensed under MIT
*/
!function(n,e){function t(e,t){this.element=e,this.settings=n.extend({},a,t),this._defaults=a,this._name=i,this.init()}var a={label:"MENU",duplicate:!0,duration:200,easingOpen:"swing",easingClose:"swing",closedSymbol:"&#9658;",openedSymbol:"&#9660;",prependTo:"body",parentTag:"a",closeOnClick:!1,allowParentLinks:!1,nestedParentLinks:!0,showChildren:!1,brand:"",init:function(){},open:function(){},close:function(){}},i="slicknav",s="slicknav";t.prototype.init=function(){var t,a,i=this,l=n(this.element),o=this.settings;if(o.duplicate?(i.mobileNav=l.clone(),i.mobileNav.removeAttr("id"),i.mobileNav.find("*").each(function(e,t){n(t).removeAttr("id")})):i.mobileNav=l,t=s+"_icon",""===o.label&&(t+=" "+s+"_no-text"),"a"==o.parentTag&&(o.parentTag='a href="#"'),i.mobileNav.attr("class",s+"_nav"),a=n('<div class="'+s+'_menu"></div>'),""!==o.brand){var r=n('<div class="'+s+'_brand">'+o.brand+"</div>");n(a).append(r)}i.btn=n(["<"+o.parentTag+' aria-haspopup="true" tabindex="0" class="'+s+"_btn "+s+'_collapsed">','<span class="'+s+'_menutxt">'+o.label+"</span>",'<span class="'+t+'">','<span class="'+s+'_icon-bar"></span>','<span class="'+s+'_icon-bar"></span>','<span class="'+s+'_icon-bar"></span>',"</span>","</"+o.parentTag+">"].join("")),n(a).append(i.btn),n(o.prependTo).prepend(a),a.append(i.mobileNav);var d=i.mobileNav.find("li");n(d).each(function(){var e=n(this),t={};if(t.children=e.children("ul").attr("role","menu"),e.data("menu",t),t.children.length>0){var a=e.contents(),l=!1;nodes=[],n(a).each(function(){return n(this).is("ul")?!1:(nodes.push(this),void(n(this).is("a")&&(l=!0)))});var r=n("<"+o.parentTag+' role="menuitem" aria-haspopup="true" tabindex="-1" class="'+s+'_item"/>');if(o.allowParentLinks&&!o.nestedParentLinks&&l)n(nodes).wrapAll('<span class="'+s+"_parent-link "+s+'_row"/>').parent();else{var d=n(nodes).wrapAll(r).parent();d.addClass(s+"_row")}e.addClass(s+"_collapsed"),e.addClass(s+"_parent");var c=n('<span class="'+s+'_arrow">'+o.closedSymbol+"</span>");o.allowParentLinks&&!o.nestedParentLinks&&l&&(c=c.wrap(r).parent()),n(nodes).last().after(c)}else 0===e.children().length&&e.addClass(s+"_txtnode");e.children("a").attr("role","menuitem").click(function(e){o.closeOnClick&&!n(e.target).parent().closest("li").hasClass(s+"_parent")&&n(i.btn).click()}),o.closeOnClick&&o.allowParentLinks&&(e.children("a").children("a").click(function(){n(i.btn).click()}),e.find("."+s+"_parent-link a:not(."+s+"_item)").click(function(){n(i.btn).click()}))}),n(d).each(function(){var e=n(this).data("menu");o.showChildren||i._visibilityToggle(e.children,null,!1,null,!0)}),i._visibilityToggle(i.mobileNav,null,!1,"init",!0),i.mobileNav.attr("role","menu"),n(e).mousedown(function(){i._outlines(!1)}),n(e).keyup(function(){i._outlines(!0)}),n(i.btn).click(function(n){n.preventDefault(),i._menuToggle()}),i.mobileNav.on("click","."+s+"_item",function(e){e.preventDefault(),i._itemClick(n(this))}),n(i.btn).keydown(function(n){var e=n||event;13==e.keyCode&&(n.preventDefault(),i._menuToggle())}),i.mobileNav.on("keydown","."+s+"_item",function(e){var t=e||event;13==t.keyCode&&(e.preventDefault(),i._itemClick(n(e.target)))}),o.allowParentLinks&&o.nestedParentLinks&&n("."+s+"_item a").click(function(n){n.stopImmediatePropagation()})},t.prototype._menuToggle=function(){var n=this,e=n.btn,t=n.mobileNav;e.hasClass(s+"_collapsed")?(e.removeClass(s+"_collapsed"),e.addClass(s+"_open")):(e.removeClass(s+"_open"),e.addClass(s+"_collapsed")),e.addClass(s+"_animating"),n._visibilityToggle(t,e.parent(),!0,e)},t.prototype._itemClick=function(n){var e=this,t=e.settings,a=n.data("menu");a||(a={},a.arrow=n.children("."+s+"_arrow"),a.ul=n.next("ul"),a.parent=n.parent(),a.parent.hasClass(s+"_parent-link")&&(a.parent=n.parent().parent(),a.ul=n.parent().next("ul")),n.data("menu",a)),a.parent.hasClass(s+"_collapsed")?(a.arrow.html(t.openedSymbol),a.parent.removeClass(s+"_collapsed"),a.parent.addClass(s+"_open"),a.parent.addClass(s+"_animating"),e._visibilityToggle(a.ul,a.parent,!0,n)):(a.arrow.html(t.closedSymbol),a.parent.addClass(s+"_collapsed"),a.parent.removeClass(s+"_open"),a.parent.addClass(s+"_animating"),e._visibilityToggle(a.ul,a.parent,!0,n))},t.prototype._visibilityToggle=function(e,t,a,i,l){var o=this,r=o.settings,d=o._getActionItems(e),c=0;a&&(c=r.duration),e.hasClass(s+"_hidden")?(e.removeClass(s+"_hidden"),e.slideDown(c,r.easingOpen,function(){n(i).removeClass(s+"_animating"),n(t).removeClass(s+"_animating"),l||r.open(i)}),e.attr("aria-hidden","false"),d.attr("tabindex","0"),o._setVisAttr(e,!1)):(e.addClass(s+"_hidden"),e.slideUp(c,this.settings.easingClose,function(){e.attr("aria-hidden","true"),d.attr("tabindex","-1"),o._setVisAttr(e,!0),e.hide(),n(i).removeClass(s+"_animating"),n(t).removeClass(s+"_animating"),l?"init"==i&&r.init():r.close(i)}))},t.prototype._setVisAttr=function(e,t){var a=this,i=e.children("li").children("ul").not("."+s+"_hidden");i.each(t?function(){var e=n(this);e.attr("aria-hidden","true");var i=a._getActionItems(e);i.attr("tabindex","-1"),a._setVisAttr(e,t)}:function(){var e=n(this);e.attr("aria-hidden","false");var i=a._getActionItems(e);i.attr("tabindex","0"),a._setVisAttr(e,t)})},t.prototype._getActionItems=function(n){var e=n.data("menu");if(!e){e={};var t=n.children("li"),a=t.find("a");e.links=a.add(t.find("."+s+"_item")),n.data("menu",e)}return e.links},t.prototype._outlines=function(e){e?n("."+s+"_item, ."+s+"_btn").css("outline",""):n("."+s+"_item, ."+s+"_btn").css("outline","none")},t.prototype.toggle=function(){var n=this;n._menuToggle()},t.prototype.open=function(){var n=this;n.btn.hasClass(s+"_collapsed")&&n._menuToggle()},t.prototype.close=function(){var n=this;n.btn.hasClass(s+"_open")&&n._menuToggle()},n.fn[i]=function(e){var a=arguments;if(void 0===e||"object"==typeof e)return this.each(function(){n.data(this,"plugin_"+i)||n.data(this,"plugin_"+i,new t(this,e))});if("string"==typeof e&&"_"!==e[0]&&"init"!==e){var s;return this.each(function(){var l=n.data(this,"plugin_"+i);l instanceof t&&"function"==typeof l[e]&&(s=l[e].apply(l,Array.prototype.slice.call(a,1)))}),void 0!==s?s:this}}}(jQuery,document,window);;
/**
 * @file
 * Custom code for the tech schedule
 */
(function ($, Drupal, window, document) {
  'use strict';
  Drupal.behaviors.techScheduleAccordion = {
    "attach": function(context, settings) {
      var self = this;
      $('.node-paper.view-mode-schedule', context).once('tech-schedule-accordion').each(function(i, val){
        var $node = $(this);
        if ($node.length) {
          var $accordion = $node.find('.group-schedule-accordion'),
              $abstract = $node.find('.field-name-field-paper-description-long');
          
          if ($abstract.children().length) {
            var $link = $(self.linkMarkup.replace('{placeholder}', self.showText));
            $accordion.before($link);
            $node.find('.tech-schedule-presentation-accordion-toggle').bind('click', function(e) {
              e.preventDefault();
              self.accordionToggle.call(this, $accordion);
            }).trigger('click');
          }
        }
      });
    },
    accordionToggle: function ($accordion) {
      var self = Drupal.behaviors.techScheduleAccordion;
      if ($(this).hasClass('is-toggled')) {
        $(this).removeClass('is-toggled').empty().append(self.showText);
        $accordion.slideUp(700);
      }
      else {
        $(this).addClass('is-toggled').empty().append(self.hideText);
        $accordion.slideDown(700);
      }
    },
    showText: 'Show details &nbsp;&#9656;',
    hideText: 'Hide details &nbsp;&#9662;',
    linkMarkup: [
       '<div class="tech-schedule-presentation-accordion-toggle-wrapper">',
       '<a href="#accordion" class="tech-schedule-presentation-accordion-toggle is-toggled">',
       '{placeholder}',
       '</a>&nbsp;&nbsp;',
       '</div>'
    ].join('')
  };

  Drupal.behaviors.techScheduleTrackAccordion = {
    "attach": function(context, settings) {
      if (!$('body').hasClass('scheme-lisa')) {
        var self = this;
        $('.node-session').once('tech-schedule-track-accordion').each(function(){
          var $node = $(this);
          var $papers = $node.find('.field-name-field-session-papers');
          $node.prepend(self.linksMarkup.replace('{hideSessionText}', self.hideSessionText));
          $papers.slideDown(0);
          $node.find('.tech-schedule-track-accordion-toggle').bind('click', function(e) {
            if ($(this).hasClass('is-toggled')) {
              $(this).removeClass('is-toggled').empty().append(self.showSessionText);
              $papers.slideUp(700);
            }
            else {
              $(this).addClass('is-toggled').empty().append(self.hideSessionText);
              $papers.slideDown(700);
            }
            e.preventDefault();
          });
        });
      }
    },
    showSessionText: 'Show details &nbsp;&#9656;',
    hideSessionText: 'Hide details &nbsp;&#9662;',
    linksMarkup: [
       '<p class="tech-schedule-track-accordion-links">',
       '<a href="#accordion" class="tech-schedule-track-accordion-toggle is-toggled">',
       '{hideSessionText}',
       '</a>',
       '</p>'
    ].join('')
  }

  Drupal.behaviors.techScheduleViewModeSwitcher = {
    'attach': function ($context, $settings) {
      $('.tech-schedule-view-mode-switcher-link').click(function (event) {
        event.preventDefault();

        $('.tech-schedule-view-mode-switcher-link.active').removeClass('active');
        $(this).addClass('active');

        var accorSelector = '.tech-schedule-track-accordion-toggle';
        var paperSelector = '.tech-schedule-presentation-accordion-toggle';

        switch ($(this).data('switcher_type')) {
          case 'condensed':
            var selector = accorSelector + '.is-toggled';
            break;

          case 'standard':
            var selector = accorSelector + ':not(.is-toggled),' + paperSelector + '.is-toggled';
            break;

          case 'expanded':
            var selector = accorSelector + ':not(.is-toggled),' + paperSelector + ':not(.is-toggled)';
            break;
        }

        $(selector).trigger('click');
      });
    }
  }

})(jQuery, Drupal, this, this.document);
;
/**
 * @file
 * Custom code for the conference training program page.
 */
(function ($, Drupal, window, document) {
  'use strict';
  Drupal.behaviors.trainingProgramAccordion = {
    "attach": function(context, settings) {
      $('.view-neat-conference-full-training-program .views-row').once('training-program-accordion').each(function(){
        var $showDetailsWrapper = $(this).find('.show-details-links');
        if ($showDetailsWrapper.length) {
          var $accordionSection = $showDetailsWrapper.nextAll();
          var $showDetails = $showDetailsWrapper.find('.show-details');
          var $hideDetails = $showDetailsWrapper.find('.hide-details');
          $showDetailsWrapper.show();
          $showDetails.bind('click', function(e) {
             e.preventDefault();
             $(this).hide();
             $hideDetails.show();
             $accordionSection.slideDown(700);
          });
          $hideDetails.bind('click', function(e) {
             e.preventDefault();
             $(this).hide();
             $showDetails.show();
             $accordionSection.slideUp(700);
          }).trigger('click');
        }
      });
    }
  }
})(jQuery, Drupal, this, this.document);
;
/**
 * @file
 * Custom code for the conference organizers section.
 */
(function ($, Drupal, window, document) {
  'use strict';
  Drupal.behaviors.organizersShowHide = {
    "attach": function(context, settings) {
      var self = this;
      $('.view-conference-organizers .attachment-before').each(function(index) {
        var view = $(this);
        view.find('.program-co-chairs').append('<a href="#" id="conference-organizers-show-hide-' + index + '" class="conference-organizers-show-hide"><span class="show">+ Show All</span><span class="hide">- Hide List<span></a>');
        var showHideLink = $('#conference-organizers-show-hide-' + index);
        self.showHideOrganizers(view, showHideLink);
        showHideLink.click(function(e) {
          e.preventDefault();
          self.showHideOrganizers(view, showHideLink);
        });
      });
    },
    showHideOrganizers: function (view, showHideLink) {
      var organizers = view.siblings('.view-content');
      if (showHideLink.hasClass('hidden')) {
        organizers.show();
        showHideLink.removeClass('hidden');
      } else { 
        organizers.hide();
        showHideLink.addClass('hidden');
      }
    }
  }
})(jQuery, Drupal, this, this.document);
;
(function($) {
  Drupal.behaviors.protectedFiles = {
    attach: function(context, settings) {
      $('.node-type-paper .group-open-access-content', context).once('paper-open-access-group', function() {
        // Move loose file access field (rendered as a lock icon) to Paper
        // field element.
        $(this).find('> .usenix-files-protected')
          .prependTo($(this).find('.field-name-field-presentation-pdf .field-item:first-child'));
      });

      $('.usenix-files-protected', context).once('usenix-files-protected', function() {
        // Add CSS class to wrapping element to make styling easier.
        $(this).parents('.field-type-file').addClass('protected-file-field');
      });
    }
  }
})(jQuery);

;
